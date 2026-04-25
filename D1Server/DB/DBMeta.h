#pragma once

#include "Core/CoreMinimal.h"

#include <cstddef>
#include <cstdint>

/** DB 메타데이터 DSL — DB_REGISTER_TABLE_* 매크로로 TableMetadata<T> 컴파일타임 특수화. DBOrm 이 CType+Offset 으로 자동 바인딩. */

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

/** 컬럼 한 개의 type-erased 메타 — offset/size/CType 으로 풀어둬 constexpr 균일 배열 가능. Size=버퍼 byte, Length=SQL N. */
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

/** TableMetadata<T> 접근자. `auto& Meta = GetTableMetadata<T>();` 문법 지원을 위해 static 인스턴스 노출. */
template<typename T>
constexpr const TableMetadata<T>& GetTableMetadata()
{
	static constexpr TableMetadata<T> Meta{};
	return Meta;
}

// 내부 preprocessor helper — SQL 타입 토큰(BIGINT/NVARCHAR(N) 등) → 문자열/ESqlCType/Length/Precision 매핑.

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

/** TableMetadata<TypeName> 특수화 시작. PkTypeSpec = BIGINT→uint64 / VARCHAR→std::string / NVARCHAR→std::wstring. */
#define DB_REGISTER_TABLE_BEGIN(TypeName, TableStr, PkTypeSpec)      \
	template<>                                                       \
	struct TableMetadata<TypeName>                                   \
	{                                                                \
		using Row = TypeName;                                        \
		using PkType = PkTypeSpec;                                   \
		static constexpr const char* TableName = TableStr;           \
		static constexpr ColumnMeta Columns[] = {

/** NOT NULL 일반 컬럼. SqlTypeTok = BIGINT/INT/NVARCHAR(32) 등 토큰. */
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

/** NULL 가능 컬럼. Row 에 SQLLEN <FieldName>_Ind; 슬롯 필요 — 매크로가 offsetof 자동 추정. */
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

/** DB_REGISTER_TABLE_BEGIN 마감. NumColumns/PkIndex 는 Columns[] 에서 constexpr 파생. */
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