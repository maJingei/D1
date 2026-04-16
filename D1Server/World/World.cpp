#include "World/World.h"

#include "World/Level.h"
#include "Network/GameServerSession.h"

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

bool World::Init(const std::string& CollisionCsvPath)
{
	std::cout << "[World] Init\n";

	bool bAllLoaded = true;
	for (int32 i = 0; i < LEVEL_COUNT; i++)
	{
		Levels[i] = std::make_shared<Level>();
		Levels[i]->Init(CollisionCsvPath, i);
	}

	return bAllLoaded;
}

void World::BeginPlay()
{
	std::cout << "[World] BeginPlay\n";
	for (int32 i = 0; i < LEVEL_COUNT; i++)
		Levels[i]->BeginPlay();
}

void World::Tick()
{
	for (int32 i = 0; i < LEVEL_COUNT; i++)
		Levels[i]->PushTickJob();
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

	Levels[LevelID]->Enter(NewID, std::move(Session));
	return NewID;
}

std::shared_ptr<Level>& World::GetLevel(int32 LevelID)
{
	if (LevelID < 0 || LevelID >= LEVEL_COUNT)
		return Levels[0];
	return Levels[LevelID];
}