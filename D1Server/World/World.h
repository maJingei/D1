#pragma once

#include "Core/CoreMinimal.h"

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
	/** 고정 Level 개수. */
	static constexpr int32 LEVEL_COUNT = 1;

	static World& GetInstance();
	static void DestroyInstance();

	/** CollisionMap 을 각 Level 에 개별 로드하고 Level 을 초기화한다. */
	bool Init(const std::string& CollisionCsvPath);

	/** 각 Level 의 BeginPlay 를 호출한다. */
	void BeginPlay();

	/** 각 Level 의 PushTickJob 을 호출한다 (Engine TimerLoop 에서 사용). */
	void Tick();

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