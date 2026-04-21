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