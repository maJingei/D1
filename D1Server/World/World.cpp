#include "World/World.h"

#include "World/Level.h"
#include "Network/GameServerSession.h"

#include <cassert>
#include <iostream>

World& World::GetInstance()
{
	static World* Instance = new World();
	return *Instance;
}

void World::DestroyInstance()
{
	// GetInstance 에서 new 로 생성한 인스턴스를 명시적으로 해제한다.
	// 정적 포인터를 nullptr 로 밀어 이중 해제를 방지한다.
	static World*& Ptr = reinterpret_cast<World*&>(GetInstance());
	delete &GetInstance();
}

bool World::Init(const std::string& ResourceBaseDir)
{
	std::cout << "[World] Init\n";

	bool bAllLoaded = true;
	for (int32 i = 0; i < LEVEL_COUNT; i++)
	{
		// LevelID 별로 Resource/<LevelFolders[i]>/Collision_Collision.csv 경로를 조립한다.
		const std::string LevelCsvPath = ResourceBaseDir + LevelFolders[i] + "\\Collision_Collision.csv";
		Levels[i] = std::make_shared<Level>();
		Levels[i]->Init(LevelCsvPath, i, LevelPortalConfigs[i]);
	}

	return bAllLoaded;
}

void World::BeginPlay()
{
	std::cout << "[World] BeginPlay\n";
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
	std::cout << "[World] Destroy\n";
}

uint64 World::EnterAnyLevel(std::shared_ptr<GameServerSession> Session)
{
	const uint64 NewID = NextPlayerID.fetch_add(1, std::memory_order_relaxed);
	const int32 LevelID = static_cast<int32>(NewID % static_cast<uint64>(LEVEL_COUNT));

	Session->SetPlayerID(NewID);
	Session->SetLevelID(LevelID);

	// 최초 입장 스폰 — 해당 Level 의 CollisionMap 에서 walkable 로 precompute 된 WalkableTiles 중 PlayerID % size 로 순환 인덱싱.
	// 포탈 전이(Level::DoPortalTransition)는 이 경로를 타지 않고 TargetSpawnTile 고정 좌표를 쓴다.
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