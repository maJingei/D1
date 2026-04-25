#pragma once

#include "Core/CoreMinimal.h"
#include "GameObject/APortalActor.h"
#include "LevelConfig.h"
#include "World/PlayerEntry.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class Level;
class GameServerSession;

/** 서버 월드 — Level 들의 생성·라이프사이클·플레이어 배정 라우팅을 담당한다. */
class World
{
public:
	/**
	 * Level 별 Portal 구성. 인덱스 = LevelID. 원형 체인 토폴로지.
	 * 모든 Level 은 우측 (29,10) 에 포탈이 있고 다음 Level ((LevelID+1) % LEVEL_COUNT) 의 좌측 (0,10) 으로 전이된다.
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

	bool Init(const std::string& ResourceBaseDir);
	void BeginPlay();
	void Tick(float DeltaTime);
	void Destroy();

	/** 서버 부팅 시 DB MAX(PlayerID) 를 NextPlayerID 로 시딩. 재시작 후에도 PK 충돌 방지. */
	void SeedNextPlayerIDFromDB();

	/** 신규 가입 시 1씩 발급되는 PlayerID. atomic fetch_add 로 단조 증가. */
	uint64 AllocNewPlayerID() { return NextPlayerID.fetch_add(1, std::memory_order_relaxed); }

	/**
	 * 봇 nameplate 표시용 서버 전역 카운터. 1 부터 단조 증가, 서버 재시작 시 1 로 reset.
	 * Handle_C_LOGIN(is_bot=true) 가 PlayerEntry.NameplateText 에 std::to_string(...) 으로 부여.
	 */
	uint32 AllocNewBotId() { return NextBotId.fetch_add(1, std::memory_order_relaxed); }

	/** AccountId 가 이미 활성 세션이면 false. 성공 시 맵에 등록. 중복 로그인 차단용. */
	bool TryRegisterAccount(const std::string& AccountId, std::shared_ptr<GameServerSession> Session);

	/** Logout 시 AccountId 를 활성 세션 맵에서 제거. */
	void UnregisterAccount(const std::string& AccountId);

	/** 로그인 확정 후 Entry 기반으로 월드 진입 스케줄. Session.PlayerID/LevelID 를 미리 채워 이후 패킷 라우팅을 안전화. */
	void EnterFromLogin(std::shared_ptr<GameServerSession> Session, PlayerEntry Entry);

	/** 전역 PlayerID 를 발급한 뒤 PlayerID % LEVEL_COUNT 로 Level 을 결정하고 비동기 입장시킨다. (C_ENTER_GAME 레거시용) */
	uint64 EnterAnyLevel(std::shared_ptr<GameServerSession> Session);

	/** LevelID [0, LEVEL_COUNT) 로 Level 을 조회한다. */
	std::shared_ptr<Level>& GetLevel(int32 LevelID);

	/** PlayerID % LEVEL_COUNT 로 Level 을 조회한다. */
	std::shared_ptr<Level>& GetLevelByPlayerID(uint64 PlayerID) { return GetLevel(static_cast<int32>(PlayerID % LEVEL_COUNT)); }

private:
	World() = default;

	std::array<std::shared_ptr<Level>, LEVEL_COUNT> Levels;

	/** 전역 PlayerID 발급 카운터. 0 은 '미입장' 예약값이므로 1부터 시작. SeedNextPlayerIDFromDB 로 재시작 시 덮어씀. */
	std::atomic<uint64> NextPlayerID{1};

	/** 서버 전역 봇 nameplate 카운터. 봇 은 메모리 전용이라 DB 시딩 없음, 항상 1 부터 시작. */
	std::atomic<uint32> NextBotId{1};

	/** 활성 AccountId → Session 맵. IOCP(OnDisconnected)↔DB 워커(Login) 공유이므로 mutex 보호. */
	std::unordered_map<std::string, std::weak_ptr<GameServerSession>> ActiveAccounts;
	std::mutex AccountsMutex;
};