#pragma once

#include "Core/CoreMinimal.h"

#include <sql.h>
#include <sqlext.h>

/**
 * ODBC 문장 핸들(SQL_HANDLE_STMT)의 RAII 래퍼.
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

	/** SQL 을 준비한다. ? 플레이스홀더 SQL 을 담아 두고 이후 BindParam* + Execute 로 넘어간다. */
	bool Prepare(const wchar_t* Sql);

	/** Prepare 된 문장을 현재 바인딩된 파라미터로 실행한다. SQL_NO_DATA(0 행)도 성공으로 취급. */
	bool Execute();

	/** 현재 커서에서 한 행을 전진시킨다. 성공 시 true, SQL_NO_DATA 또는 에러 시 false. */
	bool Fetch();

	/**
	 * ColumnIndex(1-based) 의 값을 narrow char 버퍼로 복사한다.
	 * NULL 값이면 빈 문자열을 채워 반환. BufferSize 는 바이트 단위(= 문자 수, SQL_C_CHAR).
	 */
	bool GetColumnString(SQLUSMALLINT ColumnIndex, char* OutBuffer, int32 BufferSize);

	/** INT 열을 int32 로 복사. NULL 은 0 으로 정규화. */
	bool GetColumnInt32(SQLUSMALLINT ColumnIndex, OUT int32& OutValue);

	/** BIGINT 열을 int64 로 복사. NULL 은 0 으로 정규화. */
	bool GetColumnInt64(SQLUSMALLINT ColumnIndex, OUT int64& OutValue);

	/** REAL 열을 float 로 복사. NULL 은 0.0f 로 정규화. */
	bool GetColumnFloat(SQLUSMALLINT ColumnIndex, OUT float& OutValue);

	/** ParamIndex(1-based) 슬롯에 int32 입력 파라미터를 바인딩한다. 포인터는 Execute 시점까지 유효해야 한다. */
	bool BindParamInt32(SQLUSMALLINT ParamIndex, int32* ValuePtr);

	/** ParamIndex(1-based) 슬롯에 int64 입력 파라미터를 바인딩한다. 포인터는 Execute 시점까지 유효해야 한다. */
	bool BindParamInt64(SQLUSMALLINT ParamIndex, int64* ValuePtr);

	/** ParamIndex(1-based) 슬롯에 float 입력 파라미터를 바인딩한다. 포인터는 Execute 시점까지 유효해야 한다. */
	bool BindParamFloat(SQLUSMALLINT ParamIndex, float* ValuePtr);

	/** ColumnIndex(1-based) 출력을 int32 슬롯에 바인딩한다. 포인터는 Fetch 시점까지 유효해야 한다. */
	bool BindColInt32(SQLUSMALLINT ColumnIndex, int32* OutPtr);

	/** ColumnIndex(1-based) 출력을 int64 슬롯에 바인딩한다. 포인터는 Fetch 시점까지 유효해야 한다. */
	bool BindColInt64(SQLUSMALLINT ColumnIndex, int64* OutPtr);

	/** ColumnIndex(1-based) 출력을 float 슬롯에 바인딩한다. 포인터는 Fetch 시점까지 유효해야 한다. */
	bool BindColFloat(SQLUSMALLINT ColumnIndex, float* OutPtr);

	SQLHSTMT GetHandle() const { return Hstmt; }

private:
	SQLHSTMT Hstmt = SQL_NULL_HSTMT;
};