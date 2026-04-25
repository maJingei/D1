#pragma once
#include "DB/DBMeta.h"
#include <string>

/**
 * wstring에 char형을 밀어넣는다.
 */
inline void AppendNarrowAsWide(std::wstring& OutSql, const char* Src)
{
	while (*Src != '\0') 
	{ 
	    OutSql.push_back(static_cast<wchar_t>(*Src)); 
	    ++Src;
	}
}

/**
 * "INSERT INTO <Table> (Col1, Col2, ...) VALUES (?, ?, ...);" 를 조립한다. 컬럼 순서는 TableMetadata<T>::Columns 선언 순서. PK 도 포함한다.
 */
template<typename T>
inline const std::wstring& BuildInsertSql()
{
	static const std::wstring CachedSql = []()
	{
		const auto& Meta = GetTableMetadata<T>();
		std::wstring Sql;
		Sql.reserve(256);
		Sql.append(L"INSERT INTO ");
		AppendNarrowAsWide(Sql, Meta.TableName);
		Sql.append(L" (");
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			if (i > 0) Sql.append(L", ");
			AppendNarrowAsWide(Sql, Meta.Columns[i].Name);
		}
		Sql.append(L") VALUES (");
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			if (i > 0) Sql.append(L", ");
			Sql.append(L"?");
		}
		Sql.append(L");");
		return Sql;
	}();
	return CachedSql;
}

/**
 * "SELECT Col1, Col2, ... FROM <Table> WHERE <Pk> = ?;" 를 조립한다. PK 컬럼도 SELECT 리스트에 포함 — Find 가 모든 컬럼을 동일 루프로 채우게 하는 일반성 우선 정책.
 */
template<typename T>
inline const std::wstring& BuildSelectByPkSql()
{
	static const std::wstring CachedSql = []()
	{
		const auto& Meta = GetTableMetadata<T>();
		std::wstring Sql;
		Sql.reserve(256);
		Sql.append(L"SELECT ");
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			if (i > 0) Sql.append(L", ");
			AppendNarrowAsWide(Sql, Meta.Columns[i].Name);
		}
		Sql.append(L" FROM ");
		AppendNarrowAsWide(Sql, Meta.TableName);
		Sql.append(L" WHERE ");
		AppendNarrowAsWide(Sql, Meta.Columns[Meta.PkIndex].Name);
		Sql.append(L" = ?;");
		return Sql;
	}();
	return CachedSql;
}

/**
 * "UPDATE <Table> SET Col1 = ?, ... WHERE <Pk> = ?;" 를 조립한다. SET 절은 PK 를 제외한 모든 컬럼. 마지막 ? 는 WHERE 의 PK — ParamIndex 끝 슬롯에 꽂힘.
 */
template<typename T>
inline const std::wstring& BuildUpdateSql()
{
	static const std::wstring CachedSql = []()
	{
		const auto& Meta = GetTableMetadata<T>();
		std::wstring Sql;
		Sql.reserve(256);
		Sql.append(L"UPDATE ");
		AppendNarrowAsWide(Sql, Meta.TableName);
		Sql.append(L" SET ");

		bool bFirst = true;
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			if (i == Meta.PkIndex) continue;
			if (!bFirst) Sql.append(L", ");
			bFirst = false;
			AppendNarrowAsWide(Sql, Meta.Columns[i].Name);
			Sql.append(L" = ?");
		}

		Sql.append(L" WHERE ");
		AppendNarrowAsWide(Sql, Meta.Columns[Meta.PkIndex].Name);
		Sql.append(L" = ?;");
		return Sql;
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
		std::wstring Sql;
		Sql.reserve(128);
		Sql.append(L"DELETE FROM ");
		AppendNarrowAsWide(Sql, Meta.TableName);
		Sql.append(L" WHERE ");
		AppendNarrowAsWide(Sql, Meta.Columns[Meta.PkIndex].Name);
		Sql.append(L" = ?;");
		return Sql;
	}();
	return CachedSql;
}