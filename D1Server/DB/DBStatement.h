#pragma once

#include "Core/CoreMinimal.h"

#include <sql.h>
#include <sqlext.h>

/**
 * ODBC 문장 핸들(SQL_HANDLE_STMT)의 RAII 래퍼.
 * DBConnection 이 execute 마다 생성·소멸시킨다. v1 에서는 ExecuteDirect 만 사용하며
 * Fetch/BindCol 경로는 SELECT 가 필요해지는 v2 에서 추가한다.
 */
class DBStatement
{
public:
	DBStatement(SQLHDBC InHdbc);
	~DBStatement();

	DBStatement(const DBStatement&) = delete;
	DBStatement& operator=(const DBStatement&) = delete;
	DBStatement(DBStatement&&) = delete;
	DBStatement& operator=(DBStatement&&) = delete;

	bool IsValid() const { return Hstmt != SQL_NULL_HSTMT; }

	/** 단일 SQL 문장을 그대로 실행한다. 파라미터 바인딩 없음(v1). */
	bool ExecuteDirect(const wchar_t* Sql);

	/** 현재 커서에서 한 행을 전진시킨다. 성공 시 true, SQL_NO_DATA 또는 에러 시 false. */
	bool Fetch();

	/**
	 * ColumnIndex(1-based) 의 값을 narrow char 버퍼로 복사한다.
	 * NULL 값이면 빈 문자열을 채워 반환. BufferSize 는 바이트 단위(= 문자 수, SQL_C_CHAR).
	 */
	bool GetColumnString(SQLUSMALLINT ColumnIndex, char* OutBuffer, int32 BufferSize);

	SQLHSTMT GetHandle() const { return Hstmt; }

private:
	SQLHSTMT Hstmt = SQL_NULL_HSTMT;
};