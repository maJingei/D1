#pragma once

#include "Core/CoreMinimal.h"
#include "Job/JobQueue.h"
#include "Job/Job.h"
#include "GameObject/AMonsterActor.h"
#include "Protocol.pb.h"

class GameServerSession;

/** 방에 입장한 플레이어 한 명의 상태 스냅샷. */
struct PlayerEntry
{
	uint64 PlayerID = 0;
	int32 TileX = 0;
	int32 TileY = 0;

	/** 현재 HP. 0 이 되면 bIsDead 가 true 로 전이된다. */
	int32 HP = 20;

	/** 최대 HP. S_PLAYER_DAMAGED 송신 시 함께 내려가서 클라가 게이지 비율 계산에 사용한다. */
	int32 MaxHP = 20;

	/** 1회 공격 시 대상에게 가하는 데미지. 공격자 책임 원칙 — 서버가 C_ATTACK 처리 시 이 값을 Monster.ApplyDamage 에 전달한다. */
	int32 AttackDamage = 5;

	/** 마지막 이동 방향. C_ATTACK 처리 시 1칸 앞 타일을 산출하는 데 사용한다. 스폰 직후엔 DIR_DOWN(아래)로 둔다. */
	Protocol::Direction LastDir = Protocol::DIR_DOWN;

	/** HP=0 이후 true. 추가 입력(이동/공격) 패킷을 모두 무시한다. */
	bool bIsDead = false;

	/** Client Prediction 모델에서 마지막으로 수락한 C_MOVE. */
	uint64 LastAcceptedSeq = 0;

	std::weak_ptr<GameServerSession> Session;
};

/** 서버 월드를 구성하는 단일 레벨. */
class Level : public std::enable_shared_from_this<Level>
{
public:
	void Init(const std::string& CollisionCsvPath, int32 InLevelID);
	void BeginPlay();
	void Destroy();
	
	/** 플레이어를 Level 에 비동기 입장시킨다. */
	void Enter(uint64 PlayerID, std::shared_ptr<GameServerSession> Session);	

	/** PlayerID 로 플레이어를 제거한다. */
	void Leave(uint64 PlayerID);

	/** 이동 요청을 권위적으로 검증하고 처리한다. */
	void TryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq);

	/** C_ATTACK 수신 시 큐잉. 발신 PlayerID 의 LastDir 을 기준으로 1칸 앞 몬스터를 권위적으로 검색해 데미지를 적용한다. */
	void TryAttack(uint64 PlayerID);

	bool FindNearestPlayer(FTileNode InNode, OUT uint64& OutTargetID, OUT FTileNode& OutNode);
	
	bool CalculatePlayerTileByID(uint64 PlayerID, OUT FTileNode& OutNode);
	
	static int32 ManhattanDistance(const FTileNode& A, const FTileNode& B);

	/** 현재 등록된 모든 세션에 SendBuffer 를 전송한다. */
	void Broadcast(SendBufferRef Buffer, uint64 ExceptID = 0);

	/** Engine TimerLoop 에서 호출. TickJob 을 JobQueue 에 push 한다. */
	void PushTickJob();
	
	int32 GetLevelID() const { return LevelID; }
	bool IsPlayerEmpty() const { return Players.empty(); }
	std::weak_ptr<const UCollisionMap> GetCollisionMap() const { return CollisionMap; }

public:
	/** Init 에서 분리된 충돌 맵 로드. 성공 시 CollisionMap 을 세팅하고 bCollisionLoaded 를 true 로 표시한다. */
	void LoadCollisionMap(const std::string& CollisionCsvPath);

	/** Enter 내부 구현 — Job 직렬화 안에서 실행. S_ENTER_GAME 응답 + S_SPAWN broadcast. */
	void DoEnter(uint64 PlayerID, std::shared_ptr<GameServerSession> Session);

	/** Leave 내부 구현 — Job 직렬화 안에서 실행. */
	void DoLeave(uint64 PlayerID);

	/** TryMove 내부 구현 — Job 직렬화 안에서 실행. */
	void DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq);

	/** 이동 권위 검증. */
	bool ValidateMove(const PlayerEntry& Self, int32 NextTileX, int32 NextTileY) const;

	/** TryAttack 내부 구현 — Job 직렬화 안에서 실행. */
	void DoTryAttack(uint64 PlayerID);

	/** Broadcast 내부 구현 — Job 직렬화 안에서 실행. */
	void DoBroadcast(SendBufferRef Buffer, uint64 ExceptID);

	/** 20ms 주기로 Monster 를 순회하며 이동·공격 판정을 수행한다. */
	void DoTick();

	/** 몬스터 이동 결과를 모든 클라에게 broadcast 한다. */
	void BroadcastMonsterMove(uint64 MonsterID, int32 TileX, int32 TileY);

	/** 몬스터 공격 이벤트를 broadcast 하고 대상 플레이어 HP 를 차감한다. */
	void BroadcastMonsterAttack(uint64 MonsterID, uint64 TargetPlayerID);

	/** 플레이어 피해 결과(HP/MaxHP 갱신)를 broadcast 한다. */
	void BroadcastPlayerDamaged(uint64 PlayerID, int32 HP, int32 MaxHP);

	/** 플레이어 사망을 broadcast 한다. */
	void BroadcastPlayerDied(uint64 PlayerID);

	/** 몬스터 피해 결과(HP 갱신)를 broadcast 한다. */
	void BroadcastMonsterDamaged(uint64 MonsterID, int32 HP, int32 MaxHP);

	/** 몬스터 사망을 broadcast 한다. 클라는 수신 시 해당 몬스터를 월드에서 제거한다. */
	void BroadcastMonsterDied(uint64 MonsterID);

	/** 이 레벨의 인덱스. Init 에서 세팅. */
	int32 LevelID = -1;

	/** JobQueue — composition 패턴. Level 은 JobQueue 를 상속하지 않는다. */
	std::shared_ptr<JobQueue> Queue;

	/** Level 내 플레이어 목록. JobQueue 직렬화로 보호 — 별도 락 없음. */
	std::unordered_map<uint64, PlayerEntry> Players;

	/** Level 내 몬스터 목록. 키는 내부 단조 증가 MonsterID. */
	std::unordered_map<uint64, std::shared_ptr<AMonsterActor>> Monsters;

	/** 자체 소유 CollisionMap. 다른 Level 과 공유하지 않는다. */
	std::shared_ptr<const UCollisionMap> CollisionMap;
	bool bCollisionLoaded = false;

	/** Level 내부 MonsterID 발급 카운터. */
	uint64 NextMonsterID = 1;

	/** 스폰 시작 타일. */
	static constexpr int32 SpawnBaseTileX = 5;
	static constexpr int32 SpawnBaseTileY = 10;
	static constexpr int32 SpawnStrideX = 1;

	/** 몬스터 초기 스폰 고정 좌표. */
	static constexpr int32 MonsterSpawnTileX = 10;
	static constexpr int32 MonsterSpawnTileY = 10;

	/** Tick 주파수(Hz) 및 간격(ms). 50Hz = 20ms. */
	static constexpr int32 TickHz = 10;
	static constexpr int32 TickIntervalMs = 1000 / TickHz;
};