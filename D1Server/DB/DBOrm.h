#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBBuildSql.h"
#include "DB/DBConnection.h"
#include "DB/DBStatement.h"

#include <string>

/**
 * 메타데이터 기반 템플릿 CRUD 파사드 (M3).
 * TableMetadata<T> 가 등록된 모든 타입에 대해 한 함수로 동작한다.
 *
 * v1: BuildXxxSql<T> 가 만든 인라인 wide SQL 텍스트를 SQLExecDirectW 로 흘려보낸다.
 * M4 에서 SQLBindParameter / SQLBindCol 경로로 승격될 예정.
 *
 * Find 의 partial-write 정책: i 번째 컬럼 read 가 실패하면 즉시 false 로 빠지지만,
 * [0, i) 까지의 컬럼은 이미 OutRow 에 기록된 상태로 남는다 — 임시 T 복사를 피해
 * weak_ptr 같은 런타임 필드를 무사히 보존하기 위한 트레이드오프. 실패 케이스에서
 * OutRow 를 신뢰하지 않는 것이 호출자의 책임.
 */
class DBOrm
{
public:
	/** Row 의 모든 컬럼(PK 포함) 을 INSERT. PK 중복 등으로 실패하면 false. */
	template<typename T>
	static bool Insert(DBConnection& Conn, const T& Row);

	/** PkValue(BIGINT 가정) 로 1행을 SELECT, OutRow 의 모든 등록 컬럼을 채운다. 행 없음/에러 시 false. */
	template<typename T>
	static bool Find(DBConnection& Conn, uint64 PkValue, OUT T& OutRow);

	/** Row 의 PK 로 식별된 행의 비-PK 컬럼을 Row 값으로 UPDATE. 0행이어도 ODBC 가 성공으로 반환. */
	template<typename T>
	static bool Update(DBConnection& Conn, const T& Row);

	/** PkValue 로 1행 DELETE. 0행이어도 성공으로 반환. */
	template<typename T>
	static bool Delete(DBConnection& Conn, uint64 PkValue);
};

namespace DBOrmDetail
{
	/**
	 * ColumnMeta::CType 디스패치 — Stmt 의 ColumnIndex(1-based) 값을 RowBase + Offset 에 store.
	 * INT/BIGINT/REAL 만 지원(M3 범위). 한 컬럼이라도 read 실패면 false 반환.
	 */
	inline bool ReadColumnValueFromStmt(const ColumnMeta& Col, DBStatement& Stmt, SQLUSMALLINT ColumnIndex, void* RowBase)
	{
		char* Dst = static_cast<char*>(RowBase) + Col.Offset;
		switch (Col.CType)
		{
			case ESqlCType::SBigInt:
			{
				int64 Tmp = 0;
				if (Stmt.GetColumnInt64(ColumnIndex, Tmp) == false)
					return false;
				*reinterpret_cast<int64*>(Dst) = Tmp;
				return true;
			}
			case ESqlCType::SLong:
			{
				int32 Tmp = 0;
				if (Stmt.GetColumnInt32(ColumnIndex, Tmp) == false)
					return false;
				// enum (예: Protocol::CharacterType) 도 sizeof == 4 가정으로 동일 슬롯에 박힌다.
				*reinterpret_cast<int32*>(Dst) = Tmp;
				return true;
			}
			case ESqlCType::Float:
			{
				float Tmp = 0.0f;
				if (Stmt.GetColumnFloat(ColumnIndex, Tmp) == false)
					return false;
				*reinterpret_cast<float*>(Dst) = Tmp;
				return true;
			}
		}
		return false;
	}
}

template<typename T>
bool DBOrm::Insert(DBConnection& Conn, const T& Row)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildInsertSql<T>(Row);
	return Stmt.ExecuteDirect(Sql.c_str());
}

template<typename T>
bool DBOrm::Find(DBConnection& Conn, uint64 PkValue, T& OutRow)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildSelectByPkSql<T>(PkValue);
	if (Stmt.ExecuteDirect(Sql.c_str()) == false)
		return false;

	// Fetch false = SQL_NO_DATA(행 없음) 또는 페치 실패. 둘 다 "Find 실패" 로 호출자에게 반환.
	if (Stmt.Fetch() == false)
		return false;

	// 컬럼 인덱스는 SELECT 리스트 순서(1-based) = Meta.Columns 순서(0-based) + 1.
	// 한 컬럼이라도 실패 시 즉시 반환 — partial-write 정책은 클래스 주석 참조.
	const auto& Meta = GetTableMetadata<T>();
	void* RowBase = static_cast<void*>(&OutRow);
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::ReadColumnValueFromStmt(Meta.Columns[i], Stmt, static_cast<SQLUSMALLINT>(i + 1), RowBase) == false)
			return false;
	}
	return true;
}

template<typename T>
bool DBOrm::Update(DBConnection& Conn, const T& Row)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildUpdateSql<T>(Row);
	return Stmt.ExecuteDirect(Sql.c_str());
}

template<typename T>
bool DBOrm::Delete(DBConnection& Conn, uint64 PkValue)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildDeleteByPkSql<T>(PkValue);
	return Stmt.ExecuteDirect(Sql.c_str());
}