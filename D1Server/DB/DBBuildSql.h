#pragma once

#include "DB/DBMeta.h"

#include <sstream>
#include <string>

/**
 * M3 얕은 SQL 문자열 조립기.
 * TableMetadata<T> 만 소비해 실제 실행은 하지 않는다. 생성물은 ? 플레이스홀더 기반으로,
 * M4 에서 SQLBindParameter 와 결합해 실제 바인딩/실행 경로로 승격된다.
 */

/**
 * "INSERT INTO <Table> (Col1, Col2, ...) VALUES (?, ?, ...);" 를 조립한다.
 * 컬럼 순서는 TableMetadata<T>::Columns 선언 순서를 그대로 따른다. PK 도 포함한다.
 */
template<typename T>
inline std::string BuildInsertSql()
{
	const auto& Meta = GetTableMetadata<T>();

	std::ostringstream Os;
	Os << "INSERT INTO " << Meta.TableName << " (";
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i > 0) Os << ", ";
		Os << Meta.Columns[i].Name;
	}

	Os << ") VALUES (";
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i > 0) Os << ", ";
		Os << "?";
	}
	Os << ");";
	return Os.str();
}

/**
 * "SELECT Col1, Col2, ... FROM <Table> WHERE <Pk> = ?;" 를 조립한다.
 * PK 컬럼 자체도 SELECT 리스트에 포함한다(메타 일반화 관점에서 자연스러움).
 * 정책 차이는 M4 에서 M2 DBPlayer::Find 호환성과 함께 재평가.
 */
template<typename T>
inline std::string BuildSelectByPkSql()
{
	const auto& Meta = GetTableMetadata<T>();

	std::ostringstream Os;
	Os << "SELECT ";
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (i > 0) Os << ", ";
		Os << Meta.Columns[i].Name;
	}
	Os << " FROM " << Meta.TableName
	   << " WHERE " << Meta.Columns[Meta.PkIndex].Name << " = ?;";
	return Os.str();
}