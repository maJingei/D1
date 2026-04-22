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

	const SQLRETURN Ret = ::SQLExecDirectW(Hstmt, reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(Sql)), SQL_NTS);

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
	const SQLRETURN Ret = ::SQLPrepareW(Hstmt, reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(Sql)), SQL_NTS);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLPrepareW");
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

bool DBStatement::BindParamInt32(SQLUSMALLINT ParamIndex, int32* ValuePtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	// SQL_C_SLONG ↔ SQL_INTEGER. NOT NULL 가정이라 StrLen_or_Ind 는 nullptr.
	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_SLONG, SQL_INTEGER, 0, 0,
		ValuePtr, 0, nullptr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(INT32)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamInt64(SQLUSMALLINT ParamIndex, int64* ValuePtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	// SQL_C_SBIGINT ↔ SQL_BIGINT. ColumnSize=19 가 SQL Server 가이드 값이지만 0 으로도 드라이버가 추정한다.
	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
		ValuePtr, 0, nullptr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(INT64)");
		return false;
	}
	return true;
}

bool DBStatement::BindParamFloat(SQLUSMALLINT ParamIndex, float* ValuePtr)
{
	if (Hstmt == SQL_NULL_HSTMT || ValuePtr == nullptr)
		return false;

	// SQL_C_FLOAT ↔ SQL_REAL. 4-byte single-precision, TileMoveSpeed 용.
	const SQLRETURN Ret = ::SQLBindParameter(
		Hstmt, ParamIndex, SQL_PARAM_INPUT,
		SQL_C_FLOAT, SQL_REAL, 0, 0,
		ValuePtr, 0, nullptr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindParameter(FLOAT)");
		return false;
	}
	return true;
}

bool DBStatement::BindColInt32(SQLUSMALLINT ColumnIndex, int32* OutPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_SLONG, OutPtr, sizeof(int32), nullptr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(INT32)");
		return false;
	}
	return true;
}

bool DBStatement::BindColInt64(SQLUSMALLINT ColumnIndex, int64* OutPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_SBIGINT, OutPtr, sizeof(int64), nullptr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(INT64)");
		return false;
	}
	return true;
}

bool DBStatement::BindColFloat(SQLUSMALLINT ColumnIndex, float* OutPtr)
{
	if (Hstmt == SQL_NULL_HSTMT || OutPtr == nullptr)
		return false;

	const SQLRETURN Ret = ::SQLBindCol(Hstmt, ColumnIndex, SQL_C_FLOAT, OutPtr, sizeof(float), nullptr);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Hstmt, SQL_HANDLE_STMT, L"SQLBindCol(FLOAT)");
		return false;
	}
	return true;
}