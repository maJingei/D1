#pragma once

#include "Core/CoreMinimal.h"
#include "GameObject/AActor.h"
#include "Protocol.pb.h"

class Level;
class UCollisionMap;
class GameRoom;

/** A* 탐색 노드 겸 타일 좌표 구조체. */
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

/** 몬스터 상태 열거. */
enum class EMonsterState : uint8
{
	Idle = 0,
	Move,
	Attack,
};

/** 서버 권위 몬스터 액터. */
class AMonsterActor : public AActor
{
public:
	
	AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY, std::shared_ptr<Level> InLevel, Protocol::MonsterType InMonsterType)
		: AActor(InTileX, InTileY), ParentLevel(InLevel), MonsterID(InMonsterID), MonsterType(InMonsterType)
	{
		// 기본 스탯으로 초기화. MVP 단계에서는 종류와 무관하게 동일 스탯 — 추후 종류별 차별화 시 생성자에서 분기.
		InitHP(DefaultMaxHP);
	}

	uint64 GetMonsterID() const { return MonsterID; }
	Protocol::MonsterType GetMonsterType() const { return MonsterType; }
	EMonsterState GetState() const { return State; }

	/** 공격자 인스턴스의 데미지 — Level::BroadcastMonsterAttack 이 ApplyDamage 에 전달한다. */
	int32 GetAttackDamage() const { return AttackDamage; }

	std::shared_ptr<Level> GetParentLevel() const { return ParentLevel.lock(); }

	/** 플레이어가 방을 나갔을 때 GameRoom이 호출한다. */
	void OnPlayerLeft(uint64 PlayerID) { if (LockedTargetID == PlayerID) LockedTargetID = 0; }

	/** Level Tick 안에서 호출된다. */
	void ServerTick(int32 DeltaMs);

	/** A* 경로탐색. */
	bool FindPathAStar(const FTileNode& Start, const FTileNode& Goal, std::vector<FTileNode>& OutPath) const;

	/** 맨해튼 거리 헬퍼 */
	static int32 ManhattanDistance(const FTileNode& A, const FTileNode& B);

private:
	/** Phase 1 — Idle: 레벨에 플레이어가 없으면 State 를 Idle 로 전이하고 true 반환. */
	bool TickIdle();

	/** Phase 2 — Attack: 타겟 락온(유지/재선정) + 공격·이동 쿨다운 감소 + 공격 범위 판정. */
	bool TickAttack(int32 DeltaMs, FTileNode& OutTargetTile);

	/** Phase 3 — Move: 타겟 타일이 바뀌었거나 경로가 비면 A* 재계산 후, MoveCooldown 이 끝났으면 한 칸 이동 + 브로드캐스트. */
	void TickMove(const FTileNode& TargetTile);
	
	/** 현재 포함되어 있는 Level*/
	std::weak_ptr<Level> ParentLevel;

	// ---------------------------------------------------------------
	// 식별
	// ---------------------------------------------------------------

	uint64 MonsterID = 0;

	/** 종류 식별. MVP 단계에서는 게임플레이에 영향 없음 — S_MONSTER_SPAWN/MonsterInfo 송신 시 클라 측 스프라이트 분기 키로 사용. */
	Protocol::MonsterType MonsterType = Protocol::MT_SLIME;

	// ---------------------------------------------------------------
	// 상태
	// ---------------------------------------------------------------

	EMonsterState State = EMonsterState::Idle;

	/** A* 결과 경로. 다음 목표 타일부터 순서대로 담긴다. */
	std::vector<FTileNode> CurrentPath;

	/** 현재 락온(포커스)된 타겟 PlayerID. */
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
	// 인스턴스 스탯 — 종족별/개체별로 달라질 수 있는 값
	// ---------------------------------------------------------------

	/** 1회 공격 시 대상에게 가하는 데미지. 인스턴스 멤버 — 공격자 책임 원칙. */
	int32 AttackDamage = 1;

	// ---------------------------------------------------------------
	// 튜닝값 (모든 인스턴스 공통 — 매직 넘버 금지, named constant)
	// ---------------------------------------------------------------

	/** 맨해튼 거리 이 값 이하이면 공격 범위 내로 판정. */
	static constexpr int32 AttackRangeTiles = 1;

	/** 공격 1회 후 다음 공격까지 대기 시간(밀리초). */
	static constexpr int32 AttackCooldownTotalMs = 1500;

	/** 한 칸 이동 후 다음 이동까지 대기 시간(밀리초). */
	static constexpr int32 MoveCooldownTotalMs = 1000;

	/** 스폰 시 InitHP 가 사용하는 기본 최대 HP. 추후 종족 테이블이 생기면 생성자 인자로 대체. */
	static constexpr int32 DefaultMaxHP = 10;
};
