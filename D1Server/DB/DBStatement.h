#pragma once

#include "Core/CoreMinimal.h"

#include <sql.h>
#include <sqlext.h>

/** ODBC SQL_HANDLE_STMT RAII 래퍼. Bind* 포인터 수명은 Execute/Fetch 까지 유지 필수. IndicatorPtr=nullptr → NOT NULL 고정길이. */
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

	/** 재사용 전 호출 — 직전 결과셋/커서만 닫고 Prepare plan 과 바인딩은 유지. */
	bool Reset();

	/** Prepare 된 문장을 현재 바인딩된 파라미터로 실행한다. SQL_NO_DATA(0 행)도 성공으로 취급. */
	bool Execute();

	/** 현재 커서에서 한 행을 전진시킨다. 성공 시 true, SQL_NO_DATA 또는 에러 시 false. */
	bool Fetch();

	/** ColumnIndex(1-based) 값을 narrow char 버퍼로 복사. NULL 은 빈 문자열. BufferSize 는 바이트 단위. */
	bool GetColumnString(SQLUSMALLINT ColumnIndex, char* OutBuffer, int32 BufferSize);

	/** INT 열을 int32 로 복사. NULL 은 0 으로 정규화. */
	bool GetColumnInt32(SQLUSMALLINT ColumnIndex, OUT int32& OutValue);

	/** BIGINT 열을 int64 로 복사. NULL 은 0 으로 정규화. */
	bool GetColumnInt64(SQLUSMALLINT ColumnIndex, OUT int64& OutValue);

	/** REAL 열을 float 로 복사. NULL 은 0.0f 로 정규화. */
	bool GetColumnFloat(SQLUSMALLINT ColumnIndex, OUT float& OutValue);

	/** FLOAT(53) 열을 double 로 복사. NULL 은 0.0 으로 정규화. SUM(FLOAT) 결과 수신용. */
	bool GetColumnDouble(SQLUSMALLINT ColumnIndex, OUT double& OutValue);

	// ─── BindParam 시리즈 — 입력 바인딩 (write 경로) ────────────
	bool BindParamInt32(SQLUSMALLINT ParamIndex, int32* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamInt64(SQLUSMALLINT ParamIndex, int64* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamInt16(SQLUSMALLINT ParamIndex, int16* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamUInt8(SQLUSMALLINT ParamIndex, uint8* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamFloat(SQLUSMALLINT ParamIndex, float* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamDouble(SQLUSMALLINT ParamIndex, double* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamBit(SQLUSMALLINT ParamIndex, uint8* ValuePtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindParamDate(SQLUSMALLINT ParamIndex, SQL_DATE_STRUCT* ValuePtr, SQLLEN* IndicatorPtr = nullptr);

	/** DATETIME2(p) 입력. Precision=p, ColumnSize 는 (p==0 ? 19 : 20+p) 로 자동 산정. */
	bool BindParamTimestamp(SQLUSMALLINT ParamIndex, SQL_TIMESTAMP_STRUCT* ValuePtr, uint8 Precision, SQLLEN* IndicatorPtr = nullptr);

	/** NVARCHAR(N) 입력. BufferLengthBytes=(N+1)*sizeof(wchar_t), ColumnSizeChars=N. IndicatorPtr 사실상 필수. */
	bool BindParamWChar(SQLUSMALLINT ParamIndex, wchar_t* ValuePtr, SQLLEN BufferLengthBytes, SQLLEN ColumnSizeChars, SQLLEN* IndicatorPtr = nullptr);

	/** VARCHAR(N) 입력. BufferLengthBytes = N+1, ColumnSizeBytes = N. */
	bool BindParamChar(SQLUSMALLINT ParamIndex, char* ValuePtr, SQLLEN BufferLengthBytes, SQLLEN ColumnSizeBytes, SQLLEN* IndicatorPtr = nullptr);

	/** VARBINARY(N) 입력. BufferLengthBytes = N (null term 없음), ColumnSizeBytes = N. */
	bool BindParamBinary(SQLUSMALLINT ParamIndex, uint8* ValuePtr, SQLLEN BufferLengthBytes, SQLLEN ColumnSizeBytes, SQLLEN* IndicatorPtr = nullptr);

	// ─── BindCol 시리즈 — 출력 바인딩 (read 경로) ───────────────
	bool BindColInt32(SQLUSMALLINT ColumnIndex, int32* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColInt64(SQLUSMALLINT ColumnIndex, int64* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColInt16(SQLUSMALLINT ColumnIndex, int16* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColUInt8(SQLUSMALLINT ColumnIndex, uint8* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColFloat(SQLUSMALLINT ColumnIndex, float* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColDouble(SQLUSMALLINT ColumnIndex, double* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColBit(SQLUSMALLINT ColumnIndex, uint8* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColDate(SQLUSMALLINT ColumnIndex, SQL_DATE_STRUCT* OutPtr, SQLLEN* IndicatorPtr = nullptr);
	bool BindColTimestamp(SQLUSMALLINT ColumnIndex, SQL_TIMESTAMP_STRUCT* OutPtr, SQLLEN* IndicatorPtr = nullptr);

	bool BindColWChar(SQLUSMALLINT ColumnIndex, wchar_t* OutPtr, SQLLEN BufferLengthBytes, SQLLEN* IndicatorPtr = nullptr);
	bool BindColChar(SQLUSMALLINT ColumnIndex, char* OutPtr, SQLLEN BufferLengthBytes, SQLLEN* IndicatorPtr = nullptr);
	bool BindColBinary(SQLUSMALLINT ColumnIndex, uint8* OutPtr, SQLLEN BufferLengthBytes, SQLLEN* IndicatorPtr = nullptr);

	SQLHSTMT GetHandle() const { return Hstmt; }

private:
	SQLHSTMT Hstmt = SQL_NULL_HSTMT;
};