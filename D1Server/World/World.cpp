#include "World/World.h"

#include "DB/DBConnection.h"
#include "DB/DBJobQueue.h"
#include "DB/DBStatement.h"
#include "Network/GameServerSession.h"
#include "World/Level.h"

#include <cassert>
#include <iostream>

World& World::GetInstance()
{
	static World* Instance = new World();
	return *Instance;
}

void World::DestroyInstance()
{
	static World*& Ptr = reinterpret_cast<World*&>(GetInstance());
	delete &GetInstance();
}

bool World::Init(const std::string& ResourceBaseDir)
{
	bool bAllLoaded = true;
	for (int32 i = 0; i < LEVEL_COUNT; i++)
	{
		const std::string LevelCsvPath = ResourceBaseDir + LevelFolders[i] + "\\Collision_Collision.csv";
		Levels[i] = std::make_shared<Level>();
		Levels[i]->Init(LevelCsvPath, i, LevelPortalConfigs[i]);
	}

	return bAllLoaded;
}

void World::BeginPlay()
{
	for (int32 i = 0; i < LEVEL_COUNT; i++)
		Levels[i]->BeginPlay();
}

void World::Tick(float DeltaTime)
{
	for (int32 i = 0; i < LEVEL_COUNT; i++)
		Levels[i]->Tick(DeltaTime);
}

void World::Destroy()
{
	for (int32 i = 0; i < LEVEL_COUNT; i++)
		Levels[i]->Destroy();
}

void World::SeedNextPlayerIDFromDB()
{
	DBJobQueue::GetInstance().Schedule([this](DBConnection& Conn)
	{
		DBStatement Stmt(Conn.GetHandle());
		if (Stmt.IsValid() == false)
			return;

		if (Stmt.ExecuteDirect(L"SELECT ISNULL(MAX(PlayerID), 0) FROM dbo.PlayerEntry;") == false)
			return;

		if (Stmt.Fetch() == false)
			return;

		int64 MaxId = 0;
		Stmt.GetColumnInt64(1, MaxId);
		NextPlayerID.store(static_cast<uint64>(MaxId) + 1, std::memory_order_relaxed);
	});
}

bool World::TryRegisterAccount(const std::string& AccountId, std::shared_ptr<GameServerSession> Session)
{
	std::lock_guard<std::mutex> Lock(AccountsMutex);

	// 기존 엔트리가 살아있으면 중복 로그인으로 거절. 죽은 weak_ptr 는 청소하고 덮어씀.
	auto It = ActiveAccounts.find(AccountId);
	if (It != ActiveAccounts.end() && It->second.lock() != nullptr)
		return false;

	ActiveAccounts[AccountId] = Session;
	return true;
}

void World::UnregisterAccount(const std::string& AccountId)
{
	std::lock_guard<std::mutex> Lock(AccountsMutex);
	ActiveAccounts.erase(AccountId);
}

void World::EnterFromLogin(std::shared_ptr<GameServerSession> Session, PlayerEntry Entry)
{
	const int32 TargetLevelID = Entry.LevelID;
	auto TargetLevel = GetLevel(TargetLevelID);
	if (TargetLevel == nullptr)
		return;

	// 세션의 PlayerID/LevelID 선기록 — 이후 들어오는 C_MOVE/C_ATTACK 이 올바른 Level 로 라우팅.
	Session->SetPlayerID(Entry.PlayerID);
	Session->SetLevelID(TargetLevelID);

	TargetLevel->DoAsync(&Level::DoEnterWithState, std::move(Entry), std::move(Session));
}

uint64 World::EnterAnyLevel(std::shared_ptr<GameServerSession> Session)
{
	const uint64 NewID = NextPlayerID.fetch_add(1, std::memory_order_relaxed);
	const int32 LevelID = static_cast<int32>(NewID % static_cast<uint64>(LEVEL_COUNT));

	Session->SetPlayerID(NewID);
	Session->SetLevelID(LevelID);

	const std::vector<std::pair<int32, int32>>& WalkableTiles = Levels[LevelID]->WalkableTiles;
	const size_t WalkableCount = WalkableTiles.size();
	assert(WalkableCount > 0);
	(void)WalkableCount;
	const auto& SpawnTile = WalkableTiles[NewID % WalkableTiles.size()];
	const int32 TileX = SpawnTile.first;
	const int32 TileY = SpawnTile.second;

	Levels[LevelID]->DoAsync(&Level::DoEnter, NewID, std::move(Session), TileX, TileY, false);
	return NewID;
}

std::shared_ptr<Level>& World::GetLevel(int32 LevelID)
{
	if (LevelID < 0 || LevelID >= LEVEL_COUNT)
		return Levels[0];
	return Levels[LevelID];
}