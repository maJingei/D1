#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBBuildSql.h"
#include "DB/DBConnection.h"
#include "DB/DBStatement.h"

#include <string>

/**
 * 메타데이터 기반 템플릿 CRUD 파사드 (M4).
 * TableMetadata<T> 가 등록된 모든 타입에 대해 한 함수로 동작한다.
 *
 * v2 엔진: BuildXxxSql<T> 가 ? 플레이스홀더만 박힌 SQL 을 만들고, SQLBindParameter /
 * SQLBindCol 이 C++ 스택 메모리에 직접 연결된 상태에서 SQLExecute / SQLFetch 가 한 번에
 * 값을 옮긴다 — 문자열 조립/파싱을 생략하는 진짜 바인딩 경로.
 *
 * 사용자 관점 입구는 DBContext::Set<T>() 가 반환하는 DBSet<T>. DBOrm 은 그 내부
 * 위임 대상이며 외부(서버 코드)에서 직접 호출하지 않는다 — 본 클래스는 ORM 내부 구현
 * 디테일로 취급한다. M8/M9 에서 IdentityMap / ChangeTracker 가 들어오면 호출 경로는
 * DBSet<T> → (캐시 / 추적기 경유) → DBOrm 으로 한 겹 더 두꺼워진다.
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
	 * ColumnMeta::CType 디스패치 — RowBase + Offset 위치를 ParamIndex(1-based) 슬롯에
	 * SQLBindParameter 로 바인딩. INT/BIGINT/REAL 만 지원(M3 범위 유지).
	 *
	 * RowBase 는 non-const void* — SQLBindParameter 가 input 에도 non-const 를 요구하기 때문.
	 * 호출부는 const T& Row 로 받은 뒤 const_cast<void*>(static_cast<const void*>(&Row)) 로 진입.
	 */
	inline bool BindParamFromMeta(DBStatement& Stmt, SQLUSMALLINT ParamIndex, const ColumnMeta& Col, void* RowBase)
	{
		char* Src = static_cast<char*>(RowBase) + Col.Offset;
		switch (Col.CType)
		{
			case ESqlCType::SBigInt:
				return Stmt.BindParamInt64(ParamIndex, reinterpret_cast<int64*>(Src));
			case ESqlCType::SLong:
				// enum (예: Protocol::CharacterType) 도 sizeof == 4 가정으로 동일 슬롯에 직결된다.
				return Stmt.BindParamInt32(ParamIndex, reinterpret_cast<int32*>(Src));
			case ESqlCType::Float:
				return Stmt.BindParamFloat(ParamIndex, reinterpret_cast<float*>(Src));
		}
		return false;
	}

	/**
	 * ColumnMeta::CType 디스패치 — RowBase + Offset 위치를 ColumnIndex(1-based) 슬롯에
	 * SQLBindCol 로 바인딩. Fetch 한 번에 모든 바인딩된 메모리에 값이 한꺼번에 기록된다.
	 */
	inline bool BindColFromMeta(DBStatement& Stmt, SQLUSMALLINT ColumnIndex, const ColumnMeta& Col, void* RowBase)
	{
		char* Dst = static_cast<char*>(RowBase) + Col.Offset;
		switch (Col.CType)
		{
			case ESqlCType::SBigInt:
				return Stmt.BindColInt64(ColumnIndex, reinterpret_cast<int64*>(Dst));
			case ESqlCType::SLong:
				return Stmt.BindColInt32(ColumnIndex, reinterpret_cast<int32*>(Dst));
			case ESqlCType::Float:
				return Stmt.BindColFloat(ColumnIndex, reinterpret_cast<float*>(Dst));
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

	const std::wstring Sql = BuildInsertSql<T>();
	if (Stmt.Prepare(Sql.c_str()) == false)
		return false;

	// 모든 컬럼(PK 포함) 을 선언 순서 그대로 1..N 슬롯에 바인딩.
	// ValuePtr 는 Row 의 필드 주소 — Row 의 수명이 Execute 호출까지 보장돼 있어 안전.
	const auto& Meta = GetTableMetadata<T>();
	void* RowBase = const_cast<void*>(static_cast<const void*>(&Row));
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindParamFromMeta(Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], RowBase) == false)
			return false;
	}

	return Stmt.Execute();
}

template<typename T>
bool DBOrm::Find(DBConnection& Conn, uint64 PkValue, T& OutRow)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildSelectByPkSql<T>();
	if (Stmt.Prepare(Sql.c_str()) == false)
		return false;

	// WHERE ? — PkValue 는 지역변수로 Fetch 까지 살아있다 (함수 스코프 보장).
	// SQL_C_SBIGINT 가 int64 를 요구하므로 uint64 → int64 재해석.
	if (Stmt.BindParamInt64(1, reinterpret_cast<int64*>(&PkValue)) == false)
		return false;

	// 모든 컬럼을 OutRow 의 필드 주소에 BindCol — Fetch 한 번이면 전체 store 가 끝난다.
	// partial-write 루프가 사라져 M3 의 반쪽 기록 위험도 자연 해소.
	const auto& Meta = GetTableMetadata<T>();
	void* RowBase = static_cast<void*>(&OutRow);
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindColFromMeta(Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], RowBase) == false)
			return false;
	}

	if (Stmt.Execute() == false)
		return false;

	// Fetch false = SQL_NO_DATA(행 없음) 또는 페치 실패. 둘 다 "Find 실패" 로 호출자에게 반환.
	return Stmt.Fetch();
}

template<typename T>
bool DBOrm::Update(DBConnection& Conn, const T& Row)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildUpdateSql<T>();
	if (Stmt.Prepare(Sql.c_str()) == false)
		return false;

	const auto& Meta = GetTableMetadata<T>();
	void* RowBase = const_cast<void*>(static_cast<const void*>(&Row));

	// SET 절 바인딩 — PK 를 제외한 컬럼만 순서대로 1..N-1 슬롯에.
	SQLUSMALLINT ParamIdx = 1;
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i == Meta.PkIndex) continue;
		if (DBOrmDetail::BindParamFromMeta(Stmt, ParamIdx++, Meta.Columns[i], RowBase) == false)
			return false;
	}

	// WHERE Pk = ? — Row 의 PK 슬롯 주소를 그대로 넘긴다. Row 의 수명이 Execute 까지 보장됨.
	const ColumnMeta& PkCol = Meta.Columns[Meta.PkIndex];
	int64* PkPtr = reinterpret_cast<int64*>(static_cast<char*>(RowBase) + PkCol.Offset);
	if (Stmt.BindParamInt64(ParamIdx, PkPtr) == false)
		return false;

	return Stmt.Execute();
}

template<typename T>
bool DBOrm::Delete(DBConnection& Conn, uint64 PkValue)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	const std::wstring Sql = BuildDeleteByPkSql<T>();
	if (Stmt.Prepare(Sql.c_str()) == false)
		return false;

	// PkValue 는 함수 파라미터 — Execute 까지 스코프 생존.
	if (Stmt.BindParamInt64(1, reinterpret_cast<int64*>(&PkValue)) == false)
		return false;

	return Stmt.Execute();
}