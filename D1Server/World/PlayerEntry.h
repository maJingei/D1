#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBMeta.h"
#include "Protocol.pb.h"

#include <memory>

class GameServerSession;

/**
 * 방에 입장한 플레이어 한 명의 상태 스냅샷.
 * M3 에서 Level.h 에서 이사해왔다 — 같은 파일 하단의 DB_REGISTER_TABLE 블록과 함께
 * "엔티티 = 구조체 + 스키마 선언" 번들을 형성한다.
 *
 * 영속 9 필드 (DB 매핑 대상) — 순서는 dbo.PlayerEntry 컬럼 선언 순서와 동일:
 *   PlayerID, CharacterType, LevelID, TileX, TileY, HP, MaxHP, AttackDamage, TileMoveSpeed
 * 런타임 6 필드 (DB 매핑 금지) — 하단 섹션 주석 참조.
 */
struct PlayerEntry
{
	/** 플레이어 고유 식별자. DB PK. */
	uint64 PlayerID = 0;

	/**
	 * 코스메틱 캐릭터 타입 — 스탯/행동 동일, 스프라이트만 다름. DoEnter 에서 PlayerID 해시로 자동 배정한다.
	 * DB 연동 이후엔 C_LOGIN 에 실려 온 저장값으로 대체될 예정.
	 */
	Protocol::CharacterType CharacterType = Protocol::CT_DEFAULT;

	/**
	 * 현재 소속 Level 인덱스.
	 * TODO(M5): DoEnter/DoPortalTransition 에서 Session.LevelID 로 동기화 필요.
	 *           현재는 기본값 0 이 그대로 들어감 — M2 smoke 수준에서는 무해.
	 */
	int32 LevelID = 0;

	int32 TileX = 0;
	int32 TileY = 0;

	/** 현재 HP. 0 이 되면 bIsDead 가 true 로 전이된다. */
	int32 HP = 20;

	/** 최대 HP. S_PLAYER_DAMAGED 송신 시 함께 내려가서 클라가 게이지 비율 계산에 사용한다. */
	int32 MaxHP = 20;

	/** 1회 공격 시 대상에게 가하는 데미지. 공격자 책임 원칙 — 서버가 C_ATTACK 처리 시 이 값을 Monster.ApplyDamage 에 전달한다. */
	int32 AttackDamage = 5;

	/**
	 * 타일/초 단위 이동 속도. 1 칸 이동 쿨다운(ms) = 1000(ms/sec) / TileMoveSpeed(tiles/sec). 초당 4칸 이동
	 */
	float TileMoveSpeed = 6.0f;

	// [런타임 전용, DB 매핑 제외] ----------------------------------------------
	// 아래 필드들은 DB_REGISTER_TABLE 블록에 등록되지 않으므로 TableMetadata::Columns 에 들어가지 않는다.
	// 매크로 "등록 안 하면 자연 제외" 규칙이 이 섹션의 존재 이유.

	/** 마지막 이동 방향. C_ATTACK 처리 시 1칸 앞 타일을 산출하는 데 사용한다. 스폰 직후엔 DIR_DOWN(아래)로 둔다. */
	Protocol::Direction LastDir = Protocol::DIR_DOWN;

	/** HP=0 이후 true. 추가 입력(이동/공격) 패킷을 모두 무시한다. */
	bool bIsDead = false;

	/**
	 * Portal 전이 직후 true. DoTryMove 가 성공 이동 처리 시 Portal 트리거 검사를 건너뛰고 플래그를 해제한다.
	 * TargetSpawnTile 자체가 또 다른 Portal 타일인 예외 상황에서 무한 루프를 방지하기 위한 안전장치.
	 */
	bool bJustTeleported = false;

	/** Client Prediction 모델에서 마지막으로 수락한 C_MOVE. */
	uint64 LastAcceptedSeq = 0;

	/** 서버가 직전 이동 수락 시 GetTickCount64() 로 측정해 기록한 시각(ms). 쿨다운 검증의 기준점이며 클라 패킷과 무관. */
	uint64 LastMoveTimeMs = 0;

	std::weak_ptr<GameServerSession> Session;
};

// ============================================================
// dbo.PlayerEntry 테이블 메타데이터 등록 (M3)
// 선언 순서 = Schema/PlayerEntry.sql 의 컬럼 순서와 1:1.
// 런타임 필드(bIsDead 등) 는 여기에 등록되지 않아 자연스럽게 제외된다.
// ============================================================

DB_REGISTER_TABLE_BEGIN(PlayerEntry, "dbo.PlayerEntry")
	DB_COLUMN_PK(PlayerID, BIGINT)
	DB_COLUMN(CharacterType, INT)
	DB_COLUMN(LevelID, INT)
	DB_COLUMN(TileX, INT)
	DB_COLUMN(TileY, INT)
	DB_COLUMN(HP, INT)
	DB_COLUMN(MaxHP, INT)
	DB_COLUMN(AttackDamage, INT)
	DB_COLUMN(TileMoveSpeed, REAL)
DB_REGISTER_TABLE_END()