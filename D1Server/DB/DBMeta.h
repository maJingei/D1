#pragma once

#include "Core/CoreMinimal.h"

#include <cstddef>

/**
 * DB 메타데이터 레이어 (M3).
 * 매크로 기반 DSL 로 구조체당 TableMetadata<T> 를 컴파일 타임에 특수화한다.
 * M4 의 SQLBindParameter / SQLBindCol 은 ColumnMeta::Offset + CType 을 소비해 자동 바인딩한다.
 *
 * 사용 예:
 *   DB_REGISTER_TABLE_BEGIN(PlayerEntry, "dbo.PlayerEntry")
 *       DB_COLUMN_PK(PlayerID, BIGINT)
 *       DB_COLUMN(CharacterType, INT)
 *       ...
 *   DB_REGISTER_TABLE_END()
 *
 *   const auto& Meta = GetTableMetadata<PlayerEntry>();
 */

/** ODBC SQL_C_* 상수와 1:1 매핑되는 enum. M3 범위에서는 BIGINT / INT / REAL 3종만 필요. */
enum class ESqlCType : uint8
{
	SBigInt = 0,   // SQL_C_SBIGINT / BIGINT
	SLong,         // SQL_C_SLONG   / INT
	Float,         // SQL_C_FLOAT   / REAL
};

/**
 * 컬럼 한 개의 메타데이터. type-erased 저장 — 멤버 포인터 대신 offset/size/CType 으로
 * 풀어 둬서 constexpr ColumnMeta Columns[] 균일 배열이 가능해진다.
 */
struct ColumnMeta
{
	const char* Name;
	const char* SqlType;
	bool        bIsPK;
	size_t      Offset;
	size_t      Size;
	ESqlCType   CType;
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
// 내부 preprocessor helper — SQL 타입 토큰 → 문자열 / ESqlCType 매핑
// ============================================================

#define DB_INTERNAL_SQLNAME_BIGINT "BIGINT"
#define DB_INTERNAL_SQLNAME_INT    "INT"
#define DB_INTERNAL_SQLNAME_REAL   "REAL"

#define DB_INTERNAL_CTYPE_BIGINT ESqlCType::SBigInt
#define DB_INTERNAL_CTYPE_INT    ESqlCType::SLong
#define DB_INTERNAL_CTYPE_REAL   ESqlCType::Float

#define DB_INTERNAL_CONCAT(A, B) DB_INTERNAL_CONCAT_IMPL(A, B)
#define DB_INTERNAL_CONCAT_IMPL(A, B) A##B

#define DB_INTERNAL_SQLNAME(Tok) DB_INTERNAL_CONCAT(DB_INTERNAL_SQLNAME_, Tok)
#define DB_INTERNAL_CTYPE(Tok)   DB_INTERNAL_CONCAT(DB_INTERNAL_CTYPE_,   Tok)

// ============================================================
// 사용자용 DSL 매크로
// ============================================================

/**
 * TableMetadata<TypeName> 특수화 헤더를 연다. TableStr 은 SQL 테이블 풀네임 문자열 리터럴.
 * 본 매크로 사이 DB_COLUMN / DB_COLUMN_PK 를 나열하고 DB_REGISTER_TABLE_END() 로 닫는다.
 */
#define DB_REGISTER_TABLE_BEGIN(TypeName, TableStr)                  \
	template<>                                                       \
	struct TableMetadata<TypeName>                                   \
	{                                                                \
		using Row = TypeName;                                        \
		static constexpr const char* TableName = TableStr;           \
		static constexpr ColumnMeta Columns[] = {

/**
 * 일반 컬럼 1개 등록. FieldName 은 Row 의 멤버 식별자, SqlTypeTok 은 BIGINT/INT/REAL 중 하나의 토큰.
 * offsetof 는 non-standard-layout 타입(weak_ptr 포함)에서도 MSVC 는 확장으로 지원한다.
 */
#define DB_COLUMN(FieldName, SqlTypeTok)                             \
	ColumnMeta{                                                      \
		#FieldName,                                                  \
		DB_INTERNAL_SQLNAME(SqlTypeTok),                             \
		false,                                                       \
		offsetof(Row, FieldName),                                    \
		sizeof(decltype(Row::FieldName)),                            \
		DB_INTERNAL_CTYPE(SqlTypeTok)                                \
	},

/** PK 컬럼 1개 등록. 테이블당 1개만 사용한다(복합 PK 는 M3 범위 밖). */
#define DB_COLUMN_PK(FieldName, SqlTypeTok)                          \
	ColumnMeta{                                                      \
		#FieldName,                                                  \
		DB_INTERNAL_SQLNAME(SqlTypeTok),                             \
		true,                                                        \
		offsetof(Row, FieldName),                                    \
		sizeof(decltype(Row::FieldName)),                            \
		DB_INTERNAL_CTYPE(SqlTypeTok)                                \
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