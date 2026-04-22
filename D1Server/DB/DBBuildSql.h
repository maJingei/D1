#pragma once

#include "DB/DBMeta.h"

#include <sstream>
#include <string>

/**
 * M3 인라인 SQL 빌더.
 * TableMetadata<T> + 실제 Row/PkValue 를 받아 ? 플레이스홀더 없이 값을 그대로
 * SQL 텍스트에 박아넣은 wstring 을 돌려준다. SQLExecDirectW 와 직결되는 폭이다.
 *
 * v1: 인라인 값. SQL 인젝션은 무관(서버 내부 신뢰 데이터). M4 에서 ? + SQLBindParameter
 * 경로로 승격될 예정 — 그 때 BuildXxxSql 시그니처가 다시 값-less 형태로 회귀한다.
 */

namespace DBBuildSqlDetail
{
	/**
	 * ColumnMeta::CType 디스패치 — RowBase + Offset 위치의 값을 wostringstream 에
	 * SQL 리터럴로 출력한다. INT/BIGINT/REAL 만 지원(M3 범위).
	 */
	inline void WriteColumnValueToSql(const ColumnMeta& Col, const void* RowBase, std::wostringstream& Os)
	{
		const char* Src = static_cast<const char*>(RowBase) + Col.Offset;
		switch (Col.CType)
		{
			case ESqlCType::SBigInt:
				Os << *reinterpret_cast<const int64*>(Src);
				return;
			case ESqlCType::SLong:
				Os << *reinterpret_cast<const int32*>(Src);
				return;
			case ESqlCType::Float:
				Os << *reinterpret_cast<const float*>(Src);
				return;
		}
	}
}

/**
 * "INSERT INTO <Table> (Col1, Col2, ...) VALUES (val1, val2, ...);" 를 조립한다.
 * 컬럼 순서는 TableMetadata<T>::Columns 선언 순서. PK 도 포함한다.
 */
template<typename T>
inline std::wstring BuildInsertSql(const T& Row)
{
	const auto& Meta = GetTableMetadata<T>();

	std::wostringstream Os;
	Os << L"INSERT INTO " << Meta.TableName << L" (";
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i > 0) Os << L", ";
		Os << Meta.Columns[i].Name;
	}
	Os << L") VALUES (";
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i > 0) Os << L", ";
		DBBuildSqlDetail::WriteColumnValueToSql(Meta.Columns[i], &Row, Os);
	}
	Os << L");";
	return Os.str();
}

/**
 * "SELECT Col1, Col2, ... FROM <Table> WHERE <Pk> = <PkValue>;" 를 조립한다.
 * PK 컬럼도 SELECT 리스트에 포함 — Find 가 모든 컬럼을 동일 루프로 채우게 하는 일반성 우선 정책.
 */
template<typename T>
inline std::wstring BuildSelectByPkSql(uint64 PkValue)
{
	const auto& Meta = GetTableMetadata<T>();

	std::wostringstream Os;
	Os << L"SELECT ";
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i > 0) Os << L", ";
		Os << Meta.Columns[i].Name;
	}
	Os << L" FROM " << Meta.TableName
	   << L" WHERE " << Meta.Columns[Meta.PkIndex].Name << L" = " << PkValue << L";";
	return Os.str();
}

/**
 * "UPDATE <Table> SET Col1 = val1, ... WHERE <Pk> = <PkValue>;" 를 조립한다.
 * SET 절은 PK 를 제외한 모든 컬럼. WHERE 의 PkValue 는 Row 의 PK 슬롯에서 직접 읽어 온다.
 */
template<typename T>
inline std::wstring BuildUpdateSql(const T& Row)
{
	const auto& Meta = GetTableMetadata<T>();

	std::wostringstream Os;
	Os << L"UPDATE " << Meta.TableName << L" SET ";

	bool bFirst = true;
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i == Meta.PkIndex) continue;
		if (!bFirst) Os << L", ";
		bFirst = false;
		Os << Meta.Columns[i].Name << L" = ";
		DBBuildSqlDetail::WriteColumnValueToSql(Meta.Columns[i], &Row, Os);
	}

	// PK 값은 Row 의 PK 슬롯에서 읽는다 — M3 범위에서 PK 는 BIGINT(uint64) 가정.
	const char* PkSrc = reinterpret_cast<const char*>(&Row) + Meta.Columns[Meta.PkIndex].Offset;
	const uint64 PkValue = *reinterpret_cast<const uint64*>(PkSrc);

	Os << L" WHERE " << Meta.Columns[Meta.PkIndex].Name << L" = " << PkValue << L";";
	return Os.str();
}

/**
 * "DELETE FROM <Table> WHERE <Pk> = <PkValue>;" 를 조립한다.
 */
template<typename T>
inline std::wstring BuildDeleteByPkSql(uint64 PkValue)
{
	const auto& Meta = GetTableMetadata<T>();

	std::wostringstream Os;
	Os << L"DELETE FROM " << Meta.TableName
	   << L" WHERE " << Meta.Columns[Meta.PkIndex].Name << L" = " << PkValue << L";";
	return Os.str();
}