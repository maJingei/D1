#pragma once

#include "Core/CoreMinimal.h"
#include "Job/JobQueue.h"
#include "Job/Job.h"
#include "GameObject/AMonsterActor.h"
#include "GameObject/APortalActor.h"
#include "Protocol.pb.h"
#include "World/PlayerEntry.h"

class GameServerSession;

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

	/** 로그인 경로용 — 외부에서 구성된 PlayerEntry(HP/위치 등 복원 상태 포함) 로 진입. */
	void DoEnterWithState(PlayerEntry InitialEntry, std::shared_ptr<GameServerSession> Session);

	/** Leave 내부 구현 — Job 직렬화 안에서 실행. */
	void DoLeave(uint64 PlayerID);

	/** Logout 경로 — 현재 PlayerEntry 를 DB 에 저장하고 Leave + AccountMap 에서 제거. */
	void DoLogoutAndSave(uint64 PlayerID, std::string AccountId);

	/**
	 * 현재 Level 에서 플레이어 제거 + 몬스터 락온 해제 + S_PLAYER_LEFT 브로드캐스트까지 일괄 수행.
	 * Disconnect(DoLeave) / Portal 전이(DoPortalTransition) 경로의 공통 퇴장 처리 루틴.
	 */
	void DoRemovePlayer(uint64 PlayerID);

	/**
	 * TryMove 내부 구현 — Job 직렬화 안에서 실행. Cooldown 은 서버시간(LastServerAcceptMs 대비 ServerDelta) 단독으로 검증한다.
	 * ClientDeltaMs 는 클라가 보고한 직전 송신 후 경과 ms — 검증에는 사용하지 않고 추후 reconciliation 보조값으로만 보관.
	 */
	void DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq, uint64 ClientDeltaMs);

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

	/** 채팅 broadcast 내부 구현 — Job 직렬화 안에서 S_CHAT 패킷을 만들고 같은 Level 의 모든 세션(본인 포함)에 송신. */
	void DoBroadcastChat(uint64 SenderID, std::string Text);

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