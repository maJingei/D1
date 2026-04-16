#pragma once

#include "Core/CoreMinimal.h"
#include "GameObject/AActor.h"

class Level;
class UCollisionMap;
class GameRoom;

/**
 * A* 탐색 노드 겸 타일 좌표 구조체.
 *
 * X,Y 는 타일 좌표를 나타내며 float 좌표와 혼동을 방지한다.
 * Cost 는 A* 우선순위 큐에서 사용하는 F 값(= G + H) 이다.
 */
struct FTileNode
{
	int32 X = 0;
	int32 Y = 0;
	int32 Cost = 0;

	bool operator==(const FTileNode& Other) const { return X == Other.X && Y == Other.Y; }
	bool operator!=(const FTileNode& Other) const { return !(*this == Other); }
	bool operator<(const FTileNode& Other) const { return Cost < Other.Cost; }
	bool operator>(const FTileNode& Other) const { return Cost > Other.Cost; }
};

/**
 * 몬스터 상태 열거.
 * Tick 안에서 상태 전환을 판별하는 데 사용한다.
 */
enum class EMonsterState : uint8
{
	Idle = 0,
	Move,
	Attack,
};

/**
 * 서버 권위 몬스터 액터.
 *
 * GameRoom의 Tick Job 안에서만 상태가 변경되어 경쟁 조건 없이 직렬화된다.
 * A*로 가장 가까운 플레이어를 추적하고, 맨해튼 거리 <= AttackRangeTiles 시 공격 판정을 수행한다.
 * 클라이언트로의 방송(S_MONSTER_MOVE, S_MONSTER_ATTACK 등)은 GameRoom이 담당한다.
 *
 * Non-Goal: 몬스터 HP/사망 처리는 이번 MVP 범위 외이므로 구현하지 않는다.
 */
class AMonsterActor : public AActor
{
public:
	/**
	 * @param InMonsterID  방 내 고유 몬스터 ID
	 * @param InTileX      스폰 타일 X
	 * @param InTileY      스폰 타일 Y
	 * @param InCollision  이 방의 공유 CollisionMap (읽기 전용)
	 */
	AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY, std::shared_ptr<Level> InLevel)
		: AActor(InTileX, InTileY), ParentLevel(InLevel), MonsterID(InMonsterID)
	{}

	uint64 GetMonsterID() const { return MonsterID; }
	EMonsterState GetState() const { return State; }
	static int32 GetAttackDamage() { return AttackDamage; }
	std::shared_ptr<Level> GetParentLevel() const { return ParentLevel.lock(); }

	/**
	 * 플레이어가 방을 나갔을 때 GameRoom이 호출한다.
	 * 락온 타겟이 이 플레이어였으면 락온을 해제하여 다음 Tick에 재선택이 일어나게 한다.
	 */
	void OnPlayerLeft(uint64 PlayerID) { if (LockedTargetID == PlayerID) LockedTargetID = 0; }

	/**
	 * GameRoom Tick 안에서 호출된다. 추적·이동·공격 판정을 수행한다.
	 *
	 * @param OutAttacked   이 Tick에서 공격이 발생했으면 true (S_MONSTER_ATTACK 송신 트리거)
	 * @param OutTargetID   공격 대상 PlayerID (OutAttacked == true 일 때만 유효)
	 * @param DeltaMs       Tick 간격(밀리초). 공격 쿨다운 계산에 사용.
	 */
	void ServerTick(int32 DeltaMs);
	
	/**
	 * A* 경로탐색. Start → Goal 최단 경로를 OutPath에 기록한다.
	 * OutPath는 Start 다음 타일부터 Goal까지(또는 Goal 직전까지) 순서대로 담긴다.
	 *
	 * @return  경로를 찾았으면 true
	 */
	bool FindPathAStar(const FTileNode& Start, const FTileNode& Goal, std::vector<FTileNode>& OutPath) const;

	/** 맨해튼 거리 헬퍼 */
	static int32 ManhattanDistance(const FTileNode& A, const FTileNode& B);
	
private:
	
	/** 현재 포함되어 있는 Level*/
	std::weak_ptr<Level> ParentLevel;

	// ---------------------------------------------------------------
	// 식별
	// ---------------------------------------------------------------

	uint64 MonsterID = 0;

	// ---------------------------------------------------------------
	// 상태
	// ---------------------------------------------------------------

	EMonsterState State = EMonsterState::Idle;

	/** A* 결과 경로. 다음 목표 타일부터 순서대로 담긴다. */
	std::vector<FTileNode> CurrentPath;

	/**
	 * 현재 락온(포커스)된 타겟 PlayerID.
	 * 0이면 미지정 — 이 경우 가장 가까운 플레이어를 선택하고 락온.
	 * 락온된 플레이어가 방을 나가면 ServerTick 호출부(GameRoom)에서 0으로 리셋해야 한다.
	 */
	uint64 LockedTargetID = 0;

	/** 마지막 A* 계산 시 관측한 타겟 플레이어 타일. 바뀌면 재계산. */
	FTileNode LastKnownTargetTile { -1, -1 };

	/** 공격 쿨다운 잔여 시간(밀리초). 0 이하이면 공격 가능. */
	int32 AttackCooldownMs = 0;

	/** 이동 쿨다운 잔여 시간(밀리초). 0 이하이면 한 칸 이동 가능. */
	int32 MoveCooldownMs = 0;

	// ---------------------------------------------------------------
	// 의존
	// ---------------------------------------------------------------

	/** GameRoom이 주입한 공유 CollisionMap. 읽기 전용. */
	std::shared_ptr<const UCollisionMap> CollisionMap;

	// ---------------------------------------------------------------
	// 튜닝값 (매직 넘버 금지 — 전부 named constant)
	// ---------------------------------------------------------------

	/** 맨해튼 거리 이 값 이하이면 공격 범위 내로 판정. */
	static constexpr int32 AttackRangeTiles = 1;

	/** 공격 1회 후 다음 공격까지 대기 시간(밀리초). */
	static constexpr int32 AttackCooldownTotalMs = 1500;

	/** 한 칸 이동 후 다음 이동까지 대기 시간(밀리초). */
	static constexpr int32 MoveCooldownTotalMs = 1000;

	/** 공격 1회 당 플레이어에게 가하는 데미지. */
	static constexpr int32 AttackDamage = 10;
};
