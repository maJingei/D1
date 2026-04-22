#pragma once

#include "Core/CoreMinimal.h"

#include <cstddef>
#include <cstdint>

/**
 * DB 메타데이터 레이어 (M5 풀세트).
 * 매크로 기반 DSL 로 구조체당 TableMetadata<T> 를 컴파일 타임에 특수화한다.
 * DBOrm 의 SQLBindParameter / SQLBindCol 디스패치는 ColumnMeta::CType + Offset/Size/Length/Precision 을
 * 소비해 자동 바인딩한다.
 *
 * 사용 예 (M5):
 *   DB_REGISTER_TABLE_BEGIN(PlayerEntry, "dbo.PlayerEntry")
 *       DB_COLUMN_PK(PlayerID, BIGINT)
 *       DB_COLUMN(CharacterType, INT)
 *       DB_COLUMN_NULL(NickName, NVARCHAR(32))
 *       DB_COLUMN(IsAdmin, BIT)
 *       DB_COLUMN_NULL(LastLoginAt, DATETIME2(3))
 *       DB_COLUMN_NULL(AvatarHash, VARBINARY(32))
 *   DB_REGISTER_TABLE_END()
 *
 * NULL 가능 컬럼은 Row 안에 SQLLEN <FieldName>_Ind 슬롯을 함께 선언해야 한다 — 매크로가
 * offsetof(Row, FieldName##_Ind) 로 자동 추정. 컨벤션 위반 시 컴파일 에러로 즉시 잡힘.
 */

/** ODBC SQL_C_* 상수와 1:1 매핑되는 enum. M5 풀세트 12종. */
enum class ESqlCType : uint8
{
	SBigInt = 0,   // SQL_C_SBIGINT  / BIGINT
	SLong,         // SQL_C_SLONG    / INT
	SShort,        // SQL_C_SSHORT   / SMALLINT
	UTinyInt,      // SQL_C_UTINYINT / TINYINT
	Float,         // SQL_C_FLOAT    / REAL
	Double,        // SQL_C_DOUBLE   / FLOAT  (FLOAT(53) = DOUBLE PRECISION)
	Bit,           // SQL_C_BIT      / BIT     (uint8 0/1)
	WChar,         // SQL_C_WCHAR    / NVARCHAR(N) — wchar_t[N+1]
	Char,          // SQL_C_CHAR     / VARCHAR(N)  — char[N+1]
	Date,          // SQL_C_TYPE_DATE / DATE        — SQL_DATE_STRUCT
	Timestamp,     // SQL_C_TYPE_TIMESTAMP / DATETIME2(p) — SQL_TIMESTAMP_STRUCT
	Binary,        // SQL_C_BINARY   / VARBINARY(N) — uint8[N]
};

/**
 * 컬럼 한 개의 메타데이터. type-erased 저장 — 멤버 포인터 대신 offset/size/CType 으로
 * 풀어 둬서 constexpr ColumnMeta Columns[] 균일 배열이 가능해진다.
 *
 * Size vs Length:
 *   Size   = sizeof(decltype(Row::Field)) — C 버퍼의 byte 크기 (NVARCHAR(32) 면 (32+1)*2 = 66)
 *   Length = SQL 정의의 N (NVARCHAR(32) 의 32). 고정형은 0.
 * IndicatorOffset = SIZE_MAX 면 NOT NULL — bNullable == false 와 항상 짝.
 */
struct ColumnMeta
{
	const char* Name;
	const char* SqlType;
	bool        bIsPK;
	bool        bNullable;
	size_t      Offset;
	size_t      Size;
	size_t      Length;
	size_t      IndicatorOffset;
	ESqlCType   CType;
	uint8       Precision;
	uint8       Scale;
};

/** primary template — 미정의. DB_REGISTER_TABLE_* 매크로가 T 별로 특수화를 공급한다. */
template<typename T>
struct TableMetadata;

/**
 * TableMetadata<T> 접근자. 모든 멤버가 static constexpr 라 실질 참조 없이도 동작하지만,
 * 호출부의 `auto& Meta = GetTableMetadata<T>();` 문법을 지원하기 위해 static 인스턴스를 노출한다.
 */
template<typename T>
constexpr const TableMetadata<T>& GetTableMetadata()
{
	static constexpr TableMetadata<T> Meta{};
	return Meta;
}

// ============================================================
// 내부 preprocessor helper — SQL 타입 토큰 → 문자열 / ESqlCType / Length / Precision 매핑
//
// 동작 원리:
//   고정형: DB_INTERNAL_SQLNAME(BIGINT) → DB_INTERNAL_CONCAT(DB_INTERNAL_SQLNAME_, BIGINT)
//           → DB_INTERNAL_SQLNAME_BIGINT (object-like) → "BIGINT"
//   가변형: DB_INTERNAL_SQLNAME(NVARCHAR(32))
//           → DB_INTERNAL_CONCAT(DB_INTERNAL_SQLNAME_, NVARCHAR(32))
//           → DB_INTERNAL_SQLNAME_##NVARCHAR(32)  (토큰 결합)
//           → 재스캔 시 function-like 호출로 인식 → "NVARCHAR(32)"
//
// MSVC /Zc:preprocessor 또는 표준 conformant preprocessor 가정 (VS2022 v143 기본 충족).
// ============================================================

#define DB_INTERNAL_CONCAT(A, B) DB_INTERNAL_CONCAT_IMPL(A, B)
#define DB_INTERNAL_CONCAT_IMPL(A, B) A##B

// ─── 고정형 토큰 (object-like) ─────────────────────────────────
#define DB_INTERNAL_SQLNAME_BIGINT       "BIGINT"
#define DB_INTERNAL_SQLNAME_INT          "INT"
#define DB_INTERNAL_SQLNAME_SMALLINT     "SMALLINT"
#define DB_INTERNAL_SQLNAME_TINYINT      "TINYINT"
#define DB_INTERNAL_SQLNAME_REAL         "REAL"
#define DB_INTERNAL_SQLNAME_DOUBLE       "FLOAT"      /* SQL Server: DOUBLE = FLOAT(53) */
#define DB_INTERNAL_SQLNAME_BIT          "BIT"
#define DB_INTERNAL_SQLNAME_DATE         "DATE"

#define DB_INTERNAL_CTYPE_BIGINT         ESqlCType::SBigInt
#define DB_INTERNAL_CTYPE_INT            ESqlCType::SLong
#define DB_INTERNAL_CTYPE_SMALLINT       ESqlCType::SShort
#define DB_INTERNAL_CTYPE_TINYINT        ESqlCType::UTinyInt
#define DB_INTERNAL_CTYPE_REAL           ESqlCType::Float
#define DB_INTERNAL_CTYPE_DOUBLE         ESqlCType::Double
#define DB_INTERNAL_CTYPE_BIT            ESqlCType::Bit
#define DB_INTERNAL_CTYPE_DATE           ESqlCType::Date

#define DB_INTERNAL_LENGTH_BIGINT        0
#define DB_INTERNAL_LENGTH_INT           0
#define DB_INTERNAL_LENGTH_SMALLINT      0
#define DB_INTERNAL_LENGTH_TINYINT       0
#define DB_INTERNAL_LENGTH_REAL          0
#define DB_INTERNAL_LENGTH_DOUBLE        0
#define DB_INTERNAL_LENGTH_BIT           0
#define DB_INTERNAL_LENGTH_DATE          0

#define DB_INTERNAL_PRECISION_BIGINT     0
#define DB_INTERNAL_PRECISION_INT        0
#define DB_INTERNAL_PRECISION_SMALLINT   0
#define DB_INTERNAL_PRECISION_TINYINT    0
#define DB_INTERNAL_PRECISION_REAL       0
#define DB_INTERNAL_PRECISION_DOUBLE     0
#define DB_INTERNAL_PRECISION_BIT        0
#define DB_INTERNAL_PRECISION_DATE       0

// ─── 가변형 토큰 (function-like, NVARCHAR(N) 등) ───────────────
#define DB_INTERNAL_SQLNAME_NVARCHAR(N)  "NVARCHAR(" #N ")"
#define DB_INTERNAL_SQLNAME_VARCHAR(N)   "VARCHAR("  #N ")"
#define DB_INTERNAL_SQLNAME_VARBINARY(N) "VARBINARY(" #N ")"
#define DB_INTERNAL_SQLNAME_DATETIME2(P) "DATETIME2(" #P ")"

#define DB_INTERNAL_CTYPE_NVARCHAR(N)    ESqlCType::WChar
#define DB_INTERNAL_CTYPE_VARCHAR(N)     ESqlCType::Char
#define DB_INTERNAL_CTYPE_VARBINARY(N)   ESqlCType::Binary
#define DB_INTERNAL_CTYPE_DATETIME2(P)   ESqlCType::Timestamp

#define DB_INTERNAL_LENGTH_NVARCHAR(N)    (N)
#define DB_INTERNAL_LENGTH_VARCHAR(N)     (N)
#define DB_INTERNAL_LENGTH_VARBINARY(N)   (N)
#define DB_INTERNAL_LENGTH_DATETIME2(P)   0

#define DB_INTERNAL_PRECISION_NVARCHAR(N)  0
#define DB_INTERNAL_PRECISION_VARCHAR(N)   0
#define DB_INTERNAL_PRECISION_VARBINARY(N) 0
#define DB_INTERNAL_PRECISION_DATETIME2(P) (P)

// ─── 디스패치 (concat → 재스캔, function-like 호출 자동 처리) ───
#define DB_INTERNAL_SQLNAME(Tok)    DB_INTERNAL_CONCAT(DB_INTERNAL_SQLNAME_,    Tok)
#define DB_INTERNAL_CTYPE(Tok)      DB_INTERNAL_CONCAT(DB_INTERNAL_CTYPE_,      Tok)
#define DB_INTERNAL_LENGTH(Tok)     DB_INTERNAL_CONCAT(DB_INTERNAL_LENGTH_,     Tok)
#define DB_INTERNAL_PRECISION(Tok)  DB_INTERNAL_CONCAT(DB_INTERNAL_PRECISION_,  Tok)

// ============================================================
// 사용자용 DSL 매크로
// ============================================================

/**
 * TableMetadata<TypeName> 특수화 헤더를 연다. TableStr 은 SQL 테이블 풀네임 문자열 리터럴.
 * 본 매크로 사이 DB_COLUMN / DB_COLUMN_PK / DB_COLUMN_NULL 을 나열하고
 * DB_REGISTER_TABLE_END() 로 닫는다.
 */
#define DB_REGISTER_TABLE_BEGIN(TypeName, TableStr)                  \
	template<>                                                       \
	struct TableMetadata<TypeName>                                   \
	{                                                                \
		using Row = TypeName;                                        \
		static constexpr const char* TableName = TableStr;           \
		static constexpr ColumnMeta Columns[] = {

/**
 * NOT NULL 일반 컬럼 1개. SqlTypeTok 은 BIGINT/INT/.../NVARCHAR(32) 같은 토큰.
 * offsetof 는 non-standard-layout 타입(weak_ptr 포함)에서도 MSVC 는 확장으로 지원한다.
 */
#define DB_COLUMN(FieldName, SqlTypeTok)                             \
	ColumnMeta{                                                      \
		#FieldName,                                                  \
		DB_INTERNAL_SQLNAME(SqlTypeTok),                             \
		false,                  /* bIsPK */                          \
		false,                  /* bNullable */                      \
		offsetof(Row, FieldName),                                    \
		sizeof(decltype(Row::FieldName)),                            \
		DB_INTERNAL_LENGTH(SqlTypeTok),                              \
		SIZE_MAX,               /* IndicatorOffset (NOT NULL) */     \
		DB_INTERNAL_CTYPE(SqlTypeTok),                               \
		DB_INTERNAL_PRECISION(SqlTypeTok),                           \
		0                       /* Scale (예약) */                    \
	},

/** PK 컬럼 1개. 테이블당 1개만 사용 (복합 PK 는 M6+). PK 는 정의상 NOT NULL. */
#define DB_COLUMN_PK(FieldName, SqlTypeTok)                          \
	ColumnMeta{                                                      \
		#FieldName,                                                  \
		DB_INTERNAL_SQLNAME(SqlTypeTok),                             \
		true,                                                        \
		false,                                                       \
		offsetof(Row, FieldName),                                    \
		sizeof(decltype(Row::FieldName)),                            \
		DB_INTERNAL_LENGTH(SqlTypeTok),                              \
		SIZE_MAX,                                                    \
		DB_INTERNAL_CTYPE(SqlTypeTok),                               \
		DB_INTERNAL_PRECISION(SqlTypeTok),                           \
		0                                                            \
	},

/**
 * NULL 가능 일반 컬럼 1개. 사용자는 Row 안에 SQLLEN <FieldName>_Ind; 를 함께 선언해야 한다.
 * 매크로가 offsetof(Row, FieldName##_Ind) 로 자동 추정 — 컨벤션 어기면 컴파일 에러.
 *
 * Insert 시 호출자가 <FieldName>_Ind = SQL_NULL_DATA 로 두면 NULL 저장,
 * 비-NULL 이면 SQL_NTS (string) 또는 실제 byte 길이 (binary) 를 채워야 한다.
 * Find 시 ODBC 가 <FieldName>_Ind 에 SQL_NULL_DATA 또는 실제 길이를 기록한다.
 */
#define DB_COLUMN_NULL(FieldName, SqlTypeTok)                        \
	ColumnMeta{                                                      \
		#FieldName,                                                  \
		DB_INTERNAL_SQLNAME(SqlTypeTok),                             \
		false,                                                       \
		true,                                                        \
		offsetof(Row, FieldName),                                    \
		sizeof(decltype(Row::FieldName)),                            \
		DB_INTERNAL_LENGTH(SqlTypeTok),                              \
		offsetof(Row, FieldName##_Ind),                              \
		DB_INTERNAL_CTYPE(SqlTypeTok),                               \
		DB_INTERNAL_PRECISION(SqlTypeTok),                           \
		0                                                            \
	},

/**
 * DB_REGISTER_TABLE_BEGIN 을 닫는다. NumColumns / PkIndex 는 Columns[] 에서 파생.
 * PkIndex 탐색은 constexpr 루프로 수행 — bIsPK == true 인 첫 인덱스를 반환한다.
 */
#define DB_REGISTER_TABLE_END()                                      \
		};                                                           \
		static constexpr size_t NumColumns =                         \
			sizeof(Columns) / sizeof(ColumnMeta);                    \
		static constexpr size_t PkIndex = []()                       \
		{                                                            \
			for (size_t i = 0; i < sizeof(Columns) / sizeof(ColumnMeta); ++i) \
			{                                                        \
				if (Columns[i].bIsPK) return i;                      \
			}                                                        \
			return size_t{0};                                        \
		}();                                                         \
	};