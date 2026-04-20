#pragma once

#include "Core/CoreMinimal.h"
#include "Job/JobQueue.h"
#include "Job/Job.h"
#include "GameObject/AMonsterActor.h"
#include "GameObject/APortalActor.h"
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

	/**
	 * 코스메틱 캐릭터 타입 — 스탯/행동 동일, 스프라이트만 다름. DoEnter 에서 PlayerID 해시로 자동 배정한다.
	 * DB 연동 이후엔 C_LOGIN 에 실려 온 저장값으로 대체될 예정.
	 */
	Protocol::CharacterType CharacterType = Protocol::CT_DEFAULT;

	/** HP=0 이후 true. 추가 입력(이동/공격) 패킷을 모두 무시한다. */
	bool bIsDead = false;

	/** Client Prediction 모델에서 마지막으로 수락한 C_MOVE. */
	uint64 LastAcceptedSeq = 0;

	/**
	 * Portal 전이 직후 true. DoTryMove 가 성공 이동 처리 시 Portal 트리거 검사를 건너뛰고 플래그를 해제한다.
	 * TargetSpawnTile 자체가 또 다른 Portal 타일인 예외 상황에서 무한 루프를 방지하기 위한 안전장치.
	 */
	bool bJustTeleported = false;

	/**
	 * 타일/초 단위 이동 속도. 1 칸 이동 쿨다운(ms) = 1000(ms/sec) / TileMoveSpeed(tiles/sec). 초당 4칸 이동
	 */
	float TileMoveSpeed = 6.0f;

	/** 서버가 직전 이동 수락 시 GetTickCount64() 로 측정해 기록한 시각(ms). 쿨다운 검증의 기준점이며 클라 패킷과 무관. */
	uint64 LastMoveTimeMs = 0;

	std::weak_ptr<GameServerSession> Session;
};

/** 서버 월드를 구성하는 단일 레벨. JobQueue 를 상속하여 자신이 곧 Job 직렬화 단위다. */
class Level : public JobQueue
{
public:
	void Init(const std::string& CollisionCsvPath, int32 InLevelID, const FPortalConfig& InPortalConfig);
	void BeginPlay();
	void Destroy();

	bool FindNearestPlayer(FTileNode InNode, OUT uint64& OutTargetID, OUT FTileNode& OutNode);

	bool CalculatePlayerTileByID(uint64 PlayerID, OUT FTileNode& OutNode);

	static int32 ManhattanDistance(const FTileNode& A, const FTileNode& B);

	int32 GetLevelID() const { return LevelID; }
	bool IsPlayerEmpty() const { return Players.empty(); }
	std::weak_ptr<const UCollisionMap> GetCollisionMap() const { return CollisionMap; }

public:
	/** Init 에서 분리된 충돌 맵 로드. 성공 시 CollisionMap 을 세팅하고 bCollisionLoaded 를 true 로 표시한다. */
	void LoadCollisionMap(const std::string& CollisionCsvPath);

	/**
	 * Enter 내부 구현 — Job 직렬화 안에서 실행. S_ENTER_GAME 응답 + S_SPAWN broadcast.
	 * @param bFromPortal  true 이면 진입 직후 bJustTeleported = true 로 설정하여 재진입 루프를 방지한다.
	 */
	void DoEnter(uint64 PlayerID, std::shared_ptr<GameServerSession> Session, int32 TileX, int32 TileY, bool bFromPortal);

	/** Leave 내부 구현 — Job 직렬화 안에서 실행. */
	void DoLeave(uint64 PlayerID);

	/**
	 * 현재 Level 에서 플레이어 제거 + 몬스터 락온 해제 + S_PLAYER_LEFT 브로드캐스트까지 일괄 수행.
	 * Disconnect(DoLeave) / Portal 전이(DoPortalTransition) 경로의 공통 퇴장 처리 루틴.
	 */
	void DoRemovePlayer(uint64 PlayerID);

	/** TryMove 내부 구현 — Job 직렬화 안에서 실행. */
	void DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq);

	/**
	 * Portal 접촉 후 Level 전이 내부 구현. 현재 Level 에서 플레이어를 제거하고 S_PLAYER_LEFT 를 브로드캐스트한 뒤,
	 * 대상 세션에 S_PORTAL_TELEPORT 를 전송하고 대상 Level 의 DoEnter 를 bFromPortal=true 로 큐잉한다.
	 */
	void DoPortalTransition(uint64 PlayerID, std::shared_ptr<APortalActor> Portal);

	/** 이동 권위 검증. */
	bool ValidateMove(const PlayerEntry& Self, int32 NextTileX, int32 NextTileY) const;

	/** TryAttack 내부 구현 — Job 직렬화 안에서 실행. */
	void DoTryAttack(uint64 PlayerID);

	/** Broadcast 내부 구현 — Job 직렬화 안에서 실행. */
	void DoBroadcast(SendBufferRef Buffer, uint64 ExceptID);

	/** 메인 Tick 루프에서 동기 호출. DeltaTime(초) 을 받아 Monster 들을 순회하며 이동·공격 판정을 수행한다. */
	void Tick(float DeltaTime);

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

	/** Level 내 플레이어 목록. JobQueue 직렬화로 보호 — 별도 락 없음. */
	std::unordered_map<uint64, PlayerEntry> Players;

	/** Level 내 몬스터 목록. 키는 내부 단조 증가 MonsterID. */
	std::unordered_map<uint64, std::shared_ptr<AMonsterActor>> Monsters;

	/** Level 내 Portal 목록. 키는 내부 단조 증가 PortalID. */
	std::unordered_map<uint64, std::shared_ptr<APortalActor>> Portals;

	/** 자체 소유 CollisionMap. 다른 Level 과 공유하지 않는다. */
	std::shared_ptr<const UCollisionMap> CollisionMap;
	bool bCollisionLoaded = false;

	/**
	 * CollisionMap 에서 walkable(=0) 타일 좌표를 전부 캐싱한 벡터. LoadCollisionMap 시 1회 precompute.
	 * 최초 입장 전용 — World::EnterAnyLevel 이 PlayerID % size 로 인덱싱해 초기 스폰 타일을 결정한다.
	 * 포탈 전이(DoPortalTransition)는 이 벡터를 쓰지 않고 TargetSpawnTile 고정 좌표를 그대로 사용한다.
	 */
	std::vector<std::pair<int32, int32>> WalkableTiles;

	/** Level 내부 MonsterID 발급 카운터. */
	uint64 NextMonsterID = 1;

	/** Level 내부 PortalID 발급 카운터. */
	uint64 NextPortalID = 1;

	/** 몬스터 초기 스폰 고정 좌표. */
	static constexpr int32 MonsterSpawnTileX = 10;
	static constexpr int32 MonsterSpawnTileY = 10;

	/** 플레이어 기본 이동 속도 (tiles/sec). 스폰 시 PlayerEntry::TileMoveSpeed 에 세팅하고 S_ENTER_GAME 으로 클라에 동기화. */
	static constexpr float DefaultPlayerTileMoveSpeed = 6.0f;
};