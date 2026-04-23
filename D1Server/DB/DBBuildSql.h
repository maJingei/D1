#pragma once

#include "DB/DBMeta.h"

#include <sstream>
#include <string>

/**
 * M4 SQL 빌더.
 * TableMetadata<T> 만 소비해 ? 플레이스홀더가 박힌 SQL 텍스트를 만든다. 값은 더 이상 SQL 안에
 * 박히지 않고 SQLBindParameter 로 런타임에 꽂힌다 — SQL 인젝션/정밀도 손실 면역.
 *
 * 플레이스홀더 순서 = SQL 텍스트에서 좌→우 `?` 의 등장 순서 = ParamIndex 1-based 증가 순서.
 * BuildXxxSql 과 DBOrm::Xxx 의 Bind 루프는 반드시 같은 순서를 유지해야 한다.
 */

/**
 * "INSERT INTO <Table> (Col1, Col2, ...) VALUES (?, ?, ...);" 를 조립한다.
 * 컬럼 순서는 TableMetadata<T>::Columns 선언 순서. PK 도 포함한다.
 *
 * function-local static 으로 타입당 1회만 빌드 — DBConnection::GetOrPrepare 의
 * unordered_map 키로 매번 같은 wstring 인스턴스가 들어가므로 hash/compare 비용도 최소.
 */
template<typename T>
inline const std::wstring& BuildInsertSql()
{
	static const std::wstring CachedSql = []()
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
			Os << L"?";
		}
		Os << L");";
		return Os.str();
	}();
	return CachedSql;
}

/**
 * "SELECT Col1, Col2, ... FROM <Table> WHERE <Pk> = ?;" 를 조립한다.
 * PK 컬럼도 SELECT 리스트에 포함 — Find 가 모든 컬럼을 동일 루프로 채우게 하는 일반성 우선 정책.
 */
template<typename T>
inline const std::wstring& BuildSelectByPkSql()
{
	static const std::wstring CachedSql = []()
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
		   << L" WHERE " << Meta.Columns[Meta.PkIndex].Name << L" = ?;";
		return Os.str();
	}();
	return CachedSql;
}

/**
 * "UPDATE <Table> SET Col1 = ?, ... WHERE <Pk> = ?;" 를 조립한다.
 * SET 절은 PK 를 제외한 모든 컬럼. 마지막 ? 는 WHERE 의 PK — ParamIndex 끝 슬롯에 꽂힘.
 */
template<typename T>
inline const std::wstring& BuildUpdateSql()
{
	static const std::wstring CachedSql = []()
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
			Os << Meta.Columns[i].Name << L" = ?";
		}

		Os << L" WHERE " << Meta.Columns[Meta.PkIndex].Name << L" = ?;";
		return Os.str();
	}();
	return CachedSql;
}

/**
 * "DELETE FROM <Table> WHERE <Pk> = ?;" 를 조립한다.
 */
template<typename T>
inline const std::wstring& BuildDeleteByPkSql()
{
	static const std::wstring CachedSql = []()
	{
		const auto& Meta = GetTableMetadata<T>();
		std::wostringstream Os;
		Os << L"DELETE FROM " << Meta.TableName
		   << L" WHERE " << Meta.Columns[Meta.PkIndex].Name << L" = ?;";
		return Os.str();
	}();
	return CachedSql;
}