#pragma once

#include "Core/CoreMinimal.h"
#include "GameObject/APortalActor.h"
#include "LevelConfig.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>

class Level;
class GameServerSession;

/** 서버 월드 — Level 들의 생성·라이프사이클·플레이어 배정 라우팅을 담당한다. */
class World
{
public:
	/**
	 * Level 별 Portal 구성. 인덱스 = LevelID. 원형 체인 토폴로지.
	 * 모든 Level 은 우측 (29,10) 에 포탈이 있고 다음 Level ((LevelID+1) % LEVEL_COUNT) 의 좌측 (0,10) 으로 전이된다.
	 * Level01~10 CollisionMap 전체에서 (29,10) 과 (0,10) 이 walkable(=0) 임을 실측 확인했다.
	 */
	static constexpr FPortalConfig LevelPortalConfigs[LEVEL_COUNT] = {
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 1, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 2, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 3, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 4, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 5, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 6, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 7, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 8, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 9, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
		{ /*TileX*/ 29, /*TileY*/ 10, /*TargetLevelID*/ 0, /*TargetSpawnTileX*/ 0, /*TargetSpawnTileY*/ 10 },
	};

	static World& GetInstance();
	static void DestroyInstance();

	/**
	 * Resource 베이스 디렉토리를 받아 Level 별 CollisionMap CSV 를 조립하고 각 Level 을 초기화한다.
	 * @param ResourceBaseDir  끝에 구분자 포함한 Resource 디렉토리 경로 (예: "...\\Resource\\").
	 */
	bool Init(const std::string& ResourceBaseDir);

	/** 각 Level 의 BeginPlay 를 호출한다. */
	void BeginPlay();

	/** 각 Level 의 Tick 을 DeltaTime(초) 과 함께 동기 호출한다 (메인 Tick 루프에서 사용). */
	void Tick(float DeltaTime);

	/** 각 Level 의 Destroy 를 호출한다. */
	void Destroy();

	/** 전역 PlayerID 를 발급한 뒤 PlayerID % LEVEL_COUNT 로 Level 을 결정하고 비동기 입장시킨다. */
	uint64 EnterAnyLevel(std::shared_ptr<GameServerSession> Session);

	/** LevelID [0, LEVEL_COUNT) 로 Level 을 조회한다. */
	std::shared_ptr<Level>& GetLevel(int32 LevelID);

	/** PlayerID % LEVEL_COUNT 로 Level 을 조회한다. */
	std::shared_ptr<Level>& GetLevelByPlayerID(uint64 PlayerID) { return GetLevel(static_cast<int32>(PlayerID % LEVEL_COUNT)); }

private:
	World() = default;

	std::array<std::shared_ptr<Level>, LEVEL_COUNT> Levels;

	/** 전역 PlayerID 발급 카운터. 0 은 '미입장' 예약값이므로 1부터 시작. */
	std::atomic<uint64> NextPlayerID{1};
};