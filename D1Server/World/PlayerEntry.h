#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBMeta.h"
#include "DB/DBQuery.h"
#include "Protocol.pb.h"

#include <sql.h>
#include <sqlext.h>

#include <memory>

class GameServerSession;

/** 방 입장 플레이어 1명의 상태 — 영속 필드(DB 매핑) + 런타임 필드(매핑 제외) 번들. */
struct PlayerEntry
{
	/** 플레이어 고유 식별자. DB PK. */
	uint64 PlayerID = 0;

	/** 코스메틱 캐릭터 타입(스프라이트만 다름). DoEnter 에서 PlayerID 해시로 자동 배정. */
	Protocol::CharacterType CharacterType = Protocol::CT_DEFAULT;

	/** 현재 소속 Level 인덱스. TODO(M5): DoEnter/DoPortalTransition 에서 Session.LevelID 로 동기화 필요. */
	int32 LevelID = 0;

	int32 TileX = 0;
	int32 TileY = 0;

	/** 현재 HP. 0 이 되면 bIsDead 가 true 로 전이된다. */
	int32 HP = 20;

	/** 최대 HP. S_PLAYER_DAMAGED 송신 시 함께 내려가서 클라가 게이지 비율 계산에 사용한다. */
	int32 MaxHP = 20;

	/** 1회 공격 시 대상에게 가하는 데미지. 공격자 책임 원칙 — 서버가 C_ATTACK 처리 시 이 값을 Monster.ApplyDamage 에 전달한다. */
	int32 AttackDamage = 5;

	/** 타일/초 이동 속도. 쿨다운(ms) = 1000 / TileMoveSpeed. */
	float TileMoveSpeed = 6.0f;

	// [M5 신규 영속 필드 — 풀세트 5종 검증용] ----------------------------------

	/** 닉네임. NVARCHAR(32) NULL — wchar_t[33] = 32 글자 + null term. */
	wchar_t NickName[33] = {};
	/** NickName 의 indicator. 기본 SQL_NULL_DATA — set 안 하면 NULL 저장. */
	SQLLEN NickName_Ind = SQL_NULL_DATA;

	/** 관리자 여부. BIT NOT NULL — SQL_C_BIT 가 unsigned char 0/1 요구. */
	uint8 IsAdmin = 0;

	/** 마지막 접속. DATETIME2(3) NULL — ms 단위 정밀도. */
	SQL_TIMESTAMP_STRUCT LastLoginAt = {};
	SQLLEN LastLoginAt_Ind = SQL_NULL_DATA;

	/** 평판 점수. SMALLINT NOT NULL — int16 음수 가능. */
	int16 Reputation = 0;

	/** 아바타 해시. VARBINARY(32) NULL — 32-byte 고정 폭. */
	uint8 AvatarHash[32] = {};
	SQLLEN AvatarHash_Ind = SQL_NULL_DATA;

	// [런타임 전용, DB 매핑 제외] ----------------------------------------------
	// 아래 필드들은 DB_REGISTER_TABLE 블록에 등록되지 않으므로 TableMetadata::Columns 에 들어가지 않는다.
	// 매크로 "등록 안 하면 자연 제외" 규칙이 이 섹션의 존재 이유.

	/** 마지막 이동 방향. C_ATTACK 처리 시 1칸 앞 타일을 산출하는 데 사용한다. 스폰 직후엔 DIR_DOWN(아래)로 둔다. */
	Protocol::Direction LastDir = Protocol::DIR_DOWN;

	/** HP=0 이후 true. 추가 입력(이동/공격) 패킷을 모두 무시한다. */
	bool bIsDead = false;

	/** Portal 전이 직후 true. 다음 이동 성공 시 Portal 트리거 skip + 해제 — 포탈 체인 무한 루프 방지. */
	bool bJustTeleported = false;

	/** Client Prediction 모델에서 마지막으로 수락한 C_MOVE. */
	uint64 LastAcceptedSeq = 0;

	/** 서버가 직전 이동 수락 시 GetTickCount64() 로 측정해 기록한 시각(ms). 쿨다운 검증의 기준점이며 클라 패킷과 무관. */
	uint64 LastMoveTimeMs = 0;

	std::weak_ptr<GameServerSession> Session;
};

// dbo.PlayerEntry 메타데이터 등록 — 선언 순서 = Schema/PlayerEntry.sql 컬럼 순서와 1:1. 런타임 필드는 미등록으로 자연 제외.

DB_REGISTER_TABLE_BEGIN(PlayerEntry, "dbo.PlayerEntry", uint64)
	DB_COLUMN_PK(PlayerID, BIGINT)
	DB_COLUMN(CharacterType, INT)
	DB_COLUMN(LevelID, INT)
	DB_COLUMN(TileX, INT)
	DB_COLUMN(TileY, INT)
	DB_COLUMN(HP, INT)
	DB_COLUMN(MaxHP, INT)
	DB_COLUMN(AttackDamage, INT)
	DB_COLUMN(TileMoveSpeed, REAL)
	DB_COLUMN_NULL(NickName, NVARCHAR(32))
	DB_COLUMN(IsAdmin, BIT)
	DB_COLUMN_NULL(LastLoginAt, DATETIME2(3))
	DB_COLUMN(Reputation, SMALLINT)
	DB_COLUMN_NULL(AvatarHash, VARBINARY(32))
DB_REGISTER_TABLE_END()

// LINQ ColumnProxy 묶음. 인덱스는 DB_REGISTER_TABLE_BEGIN 블록의 DB_COLUMN 선언 순서와 1:1.

struct PlayerEntryCol
{
	static constexpr ColumnProxy<PlayerEntry, 0> PlayerID{};
	static constexpr ColumnProxy<PlayerEntry, 1> CharacterType{};
	static constexpr ColumnProxy<PlayerEntry, 2> LevelID{};
	static constexpr ColumnProxy<PlayerEntry, 3> TileX{};
	static constexpr ColumnProxy<PlayerEntry, 4> TileY{};
	static constexpr ColumnProxy<PlayerEntry, 5> HP{};
	static constexpr ColumnProxy<PlayerEntry, 6> MaxHP{};
	static constexpr ColumnProxy<PlayerEntry, 7> AttackDamage{};
	static constexpr ColumnProxy<PlayerEntry, 8> TileMoveSpeed{};
	static constexpr ColumnProxy<PlayerEntry, 9> NickName{};
	static constexpr ColumnProxy<PlayerEntry, 10> IsAdmin{};
	static constexpr ColumnProxy<PlayerEntry, 11> LastLoginAt{};
	static constexpr ColumnProxy<PlayerEntry, 12> Reputation{};
	static constexpr ColumnProxy<PlayerEntry, 13> AvatarHash{};
};