#include "DB/DBStatement.h"
#include "DB/DBConnection.h"

DBStatement::DBStatement(SQLHDBC InHdbc)
{
	const SQLRETURN Ret = ::SQLAllocHandle(SQL_HANDLE_STMT, InHdbc, &Hstmt);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(InHdbc, SQL_HANDLE_DBC, L"SQLAllocHandle(STMT)");
		Hstmt = SQL_NULL_HSTMT;
	}
}

DBStatement::~DBStatement()
{
	if (Hstmt != SQL_NULL_HSTMT)
	{
		// SQLExecDirect 후 미처리 결과셋이 남아있을 수 있으므로 Close 후 해제.
		::SQLFreeStmt(Hstmt, SQL_CLOSE);
		::SQLFreeHandle(SQL_HANDLE_STMT, Hstmt);
		Hstmt = SQL_NULL_HSTMT;
	}
}

bool DBStatement::ExecuteDirect(const wchar_t* Sql)
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	const SQLRETURN Ret = ::SQLExecDirectW(Hstmt, (const_cast<wchar_t*>(Sql)), SQL_NTS);

	// SQL_NO_DATA: UPDATE/DELETE 가 0 행을 건드린 경우. 성공으로 취급.
	if (Ret == SQL_SUCCESS || Ret == SQL_SUCCESS_WITH_INFO || Ret == SQL_NO_DATA)
		return true;

	DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLExecDirectW");
	return false;
}

bool DBStatement::Prepare(const wchar_t* Sql)
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQLPrepareW 는 드라이버에 SQL 텍스트를 등록만 한다 — 실제 전송은 SQLExecute.
	const SQLRETURN Ret = ::SQLPrepareW(Hstmt, (const_cast<wchar_t*>(Sql)), SQL_NTS);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLPrepareW");
		return false;
	}
	return true;
}

bool DBStatement::Reset()
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQL_CLOSE 는 결과셋·커서만 닫는다. 바인딩은 유지되며 다음 SQLBindParameter/SQLBindCol 가
	// 같은 슬롯에 새 주소를 등록하면 자동으로 덮어쓴다 — 따라서 SQL_RESET_PARAMS/SQL_UNBIND 는 불필요.
	const SQLRETURN Ret = ::SQLFreeStmt(Hstmt, SQL_CLOSE);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLFreeStmt(SQL_CLOSE)");
		return false;
	}
	return true;
}

bool DBStatement::Execute()
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQL_NO_DATA: UPDATE/DELETE 가 0 행을 건드린 경우. 성공으로 취급 (ExecuteDirect 와 동일 의미).
	const SQLRETURN Ret = ::SQLExecute(Hstmt);
	
	if (Ret == SQL_SUCCESS || Ret == SQL_SUCCESS_WITH_INFO || Ret == SQL_NO_DATA)
		return true;

	DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLExecute");
	return false;
}

bool DBStatement::Fetch()
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQLFetch 는 결과 커서를 한 행 전진시킨다. SQL_NO_DATA 는 "행 없음/끝" 을 의미하며
	// 여기서는 명시적으로 에러로 취급하지 않고 false 만 반환해 호출자가 루프 종료로 해석하게 한다.
	const SQLRETURN Ret = ::SQLFetch(Hstmt);
	if (Ret == SQL_NO_DATA)
		return false;

	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLFetch");
		return false;
	}
	return true;
}

bool DBStatement::GetColumnString(SQLUSMALLINT ColumnIndex, char* OutBuffer, int32 BufferSize)
{
	if (Hstmt == SQL_NULL_HSTMT || OutBuffer == nullptr || BufferSize <= 0)
		return false;

	// SQL_C_CHAR 는 narrow char 로 변환해 가져온다. Indicator 는 실제 바이트 수 또는 SQL_NULL_DATA.
	SQLLEN Indicator = 0;
	const SQLRETURN Ret = ::SQLGetData(Hstmt, ColumnIndex, SQL_C_CHAR, OutBuffer, BufferSize, &Indicator);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLGetData");
		OutBuffer[0] = '\0';
		return false;
	}

	// NULL 열은 빈 문자열로 정규화. SQLGetData 는 이 경우 버퍼를 건드리지 않는다.
	if (Indicator == SQL_NULL_DATA)
		OutBuffer[0] = '\0';

	return true;
}

bool DBStatement::GetColumnInt32(SQLUSMALLINT ColumnIndex, int32& OutValue)
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQL_C_SLONG 은 4바이트 signed 정수 — SQL Server INT 타입과 1:1.
	SQLLEN Indicator = 0;
	SQLINTEGER Value = 0;
	const SQLRETURN Ret = ::SQLGetData(Hstmt, ColumnIndex, SQL_C_SLONG, &Value, sizeof(Value), &Indicator);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLGetData(INT32)");
		OutValue = 0;
		return false;
	}

	OutValue = (Indicator == SQL_NULL_DATA) ? 0 : static_cast<int32>(Value);
	return true;
}

bool DBStatement::GetColumnInt64(SQLUSMALLINT ColumnIndex, int64& OutValue)
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQL_C_SBIGINT 는 8바이트 signed 정수 — SQL Server BIGINT 타입과 1:1. PlayerID 읽기에 사용.
	SQLLEN Indicator = 0;
	SQLBIGINT Value = 0;
	const SQLRETURN Ret = ::SQLGetData(Hstmt, ColumnIndex, SQL_C_SBIGINT, &Value, sizeof(Value), &Indicator);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLGetData(INT64)");
		OutValue = 0;
		return false;
	}

	OutValue = (Indicator == SQL_NULL_DATA) ? 0 : static_cast<int64>(Value);
	return true;
}

bool DBStatement::GetColumnFloat(SQLUSMALLINT ColumnIndex, float& OutValue)
{
	if (Hstmt == SQL_NULL_HSTMT)
		return false;

	// SQL_C_FLOAT 는 4바이트 single-precision — SQL Server REAL 과 1:1. TileMoveSpeed 읽기에 사용.
	SQLLEN Indicator = 0;
	SQLREAL Value = 0.0f;
	const SQLRETURN Ret = ::SQLGetData(Hstmt, ColumnIndex, SQL_C_FLOAT, &Value, sizeof(Value), &Indicator);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLGetData(FLOAT)");
		OutValue = 0.0f;
		return false;
	}

	OutValue = (Indicator == SQL_NULL_DATA) ? 0.0f : static_cast<float>(Value);
	return true;
}

// ============================================================
// BindParam — 입력 바인딩 (write 경로)
// ============================================================

bool DBStatement::BindParamInt32(SQLUSMALLINT ParamIndex, int32* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_SLONG, SQL_INTEGER, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(INT32)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamInt64(SQLUSMALLINT ParamIndex, int64* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(INT64)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamInt16(SQLUSMALLINT ParamIndex, int16* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_SSHORT, SQL_SMALLINT, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(INT16)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamUInt8(SQLUSMALLINT ParamIndex, uint8* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_UTINYINT, SQL_TINYINT, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(TINYINT)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamFloat(SQLUSMALLINT ParamIndex, float* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_FLOAT, SQL_REAL, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(FLOAT)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamDouble(SQLUSMALLINT ParamIndex, double* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(DOUBLE)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamBit(SQLUSMALLINT ParamIndex, uint8* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_BIT, SQL_BIT, 0, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(BIT)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamDate(SQLUSMALLINT ParamIndex, SQL_DATE_STRUCT* ValuePtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_TYPE_DATE, SQL_TYPE_DATE, 10, 0,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(DATE)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamTimestamp(SQLUSMALLINT ParamIndex, SQL_TIMESTAMP_STRUCT* ValuePtr, uint8 Precision, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	// DATETIME2(p) ColumnSize 는 ODBC 표준 공식: p == 0 ? 19 : 20 + p
	// 예: DATETIME2(3) → "2026-04-22 12:34:56.789" = 23 글자
	const SQLULEN ColumnSize = (Precision == 0) ? 19 : (20u + Precision);
	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP,
		ColumnSize, Precision,
		ValuePtr, 0, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(TIMESTAMP)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamWChar(SQLUSMALLINT ParamIndex, wchar_t* ValuePtr, SQLLEN BufferLengthBytes, SQLLEN ColumnSizeChars, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	// SQL_C_WCHAR ↔ SQL_WVARCHAR. ColumnSize = SQL 측 글자 수 (NVARCHAR(N) 의 N), BufferLength = C 버퍼 byte.
	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_WCHAR, SQL_WVARCHAR,
		static_cast<SQLULEN>(ColumnSizeChars), 0,
		ValuePtr, BufferLengthBytes, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(WCHAR)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamChar(SQLUSMALLINT ParamIndex, char* ValuePtr, SQLLEN BufferLengthBytes, SQLLEN ColumnSizeBytes, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_CHAR, SQL_VARCHAR,
		static_cast<SQLULEN>(ColumnSizeBytes), 0,
		ValuePtr, BufferLengthBytes, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(CHAR)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamBinary(SQLUSMALLINT ParamIndex, uint8* ValuePtr, SQLLEN BufferLengthBytes, SQLLEN ColumnSizeBytes, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_BINARY, SQL_VARBINARY,
		static_cast<SQLULEN>(ColumnSizeBytes), 0,
		ValuePtr, BufferLengthBytes, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(BINARY)");
		return false;
	}
	return true;
}

// ============================================================
// BindCol — 출력 바인딩 (read 경로)
// ============================================================

bool DBStatement::BindColInt32(SQLUSMALLINT ColumnIndex, int32* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_SLONG, OutPtr, sizeof(int32), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(INT32)");
		return false;
	}
	return true;
}

bool DBStatement::BindColInt64(SQLUSMALLINT ColumnIndex, int64* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_SBIGINT, OutPtr, sizeof(int64), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(INT64)");
		return false;
	}
	return true;
}

bool DBStatement::BindColInt16(SQLUSMALLINT ColumnIndex, int16* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_SSHORT, OutPtr, sizeof(int16), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(INT16)");
		return false;
	}
	return true;
}

bool DBStatement::BindColUInt8(SQLUSMALLINT ColumnIndex, uint8* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_UTINYINT, OutPtr, sizeof(uint8), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(TINYINT)");
		return false;
	}
	return true;
}

bool DBStatement::BindColFloat(SQLUSMALLINT ColumnIndex, float* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_FLOAT, OutPtr, sizeof(float), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(FLOAT)");
		return false;
	}
	return true;
}

bool DBStatement::BindColDouble(SQLUSMALLINT ColumnIndex, double* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_DOUBLE, OutPtr, sizeof(double), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(DOUBLE)");
		return false;
	}
	return true;
}

bool DBStatement::BindColBit(SQLUSMALLINT ColumnIndex, uint8* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_BIT, OutPtr, sizeof(uint8), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(BIT)");
		return false;
	}
	return true;
}

bool DBStatement::BindColDate(SQLUSMALLINT ColumnIndex, SQL_DATE_STRUCT* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_TYPE_DATE, OutPtr, sizeof(SQL_DATE_STRUCT), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(DATE)");
		return false;
	}
	return true;
}

bool DBStatement::BindColTimestamp(SQLUSMALLINT ColumnIndex, SQL_TIMESTAMP_STRUCT* OutPtr, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_TYPE_TIMESTAMP, OutPtr, sizeof(SQL_TIMESTAMP_STRUCT), IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(TIMESTAMP)");
		return false;
	}
	return true;
}

bool DBStatement::BindColWChar(SQLUSMALLINT ColumnIndex, wchar_t* OutPtr,
                               SQLLEN BufferLengthBytes, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_WCHAR, OutPtr, BufferLengthBytes, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(WCHAR)");
		return false;
	}
	return true;
}

bool DBStatement::BindColChar(SQLUSMALLINT ColumnIndex, char* OutPtr,
                              SQLLEN BufferLengthBytes, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_CHAR, OutPtr, BufferLengthBytes, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(CHAR)");
		return false;
	}
	return true;
}

bool DBStatement::BindColBinary(SQLUSMALLINT ColumnIndex, uint8* OutPtr,
                                SQLLEN BufferLengthBytes, SQLLEN* IndicatorPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_BINARY, OutPtr, BufferLengthBytes, IndicatorPtr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(BINARY)");
		return false;
	}
	return true;
}