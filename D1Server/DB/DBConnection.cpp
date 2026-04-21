#include "DB/DBConnection.h"
#include "DB/DBStatement.h"

#include <iostream>

DBConnection::DBConnection(SQLHENV InHenv)
{
	// HENV 아래 HDBC 를 할당. Pool 이 HENV 소유권을 갖고 있으므로 여기서는 할당만 수행.
	const SQLRETURN Ret = ::SQLAllocHandle(SQL_HANDLE_DBC, InHenv, &Hdbc);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		HandleError(InHenv, SQL_HANDLE_ENV, L"SQLAllocHandle(DBC)");
		Hdbc = SQL_NULL_HDBC;
	}
}

DBConnection::~DBConnection()
{
	Disconnect();
}

bool DBConnection::Connect(const wchar_t* ConnectString)
{
	if (Hdbc == SQL_NULL_HDBC)
		return false;

	// OutConnectionString 은 서버가 실제로 사용한 접속 문자열을 돌려받는 버퍼.
	// 현재는 사용하지 않지만 SQLDriverConnectW 는 유효한 버퍼를 요구한다.
	SQLWCHAR OutConnectionString[1024] = { 0 };
	SQLSMALLINT OutConnectionStringLength = 0;

	const SQLRETURN Ret = ::SQLDriverConnectW(
		Hdbc,
		nullptr,
		reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(ConnectString)),
		SQL_NTS,
		OutConnectionString,
		static_cast<SQLSMALLINT>(sizeof(OutConnectionString) / sizeof(SQLWCHAR)),
		&OutConnectionStringLength,
		SQL_DRIVER_NOPROMPT);

	if (SQL_SUCCEEDED(Ret) == false)
	{
		HandleError(Hdbc, SQL_HANDLE_DBC, L"SQLDriverConnectW");
		return false;
	}

	return true;
}

bool DBConnection::Execute(const wchar_t* Sql)
{
	if (Hdbc == SQL_NULL_HDBC)
		return false;

	// DBStatement RAII: 스코프 벗어나면 SQLHSTMT 자동 해제.
	DBStatement Stmt(Hdbc);
	if (Stmt.IsValid() == false)
		return false;

	return Stmt.ExecuteDirect(Sql);
}

void DBConnection::Disconnect()
{
	if (Hdbc != SQL_NULL_HDBC)
	{
		// SQLDisconnect 는 이미 연결되어 있지 않아도 안전(상태 검사 후 성공 반환).
		::SQLDisconnect(Hdbc);
		::SQLFreeHandle(SQL_HANDLE_DBC, Hdbc);
		Hdbc = SQL_NULL_HDBC;
	}
}

void DBConnection::HandleError(SQLHANDLE Handle, SQLSMALLINT HandleType, const wchar_t* Context)
{
	// 여러 개의 진단 레코드가 있을 수 있으므로 1번부터 순회하며 전부 로그.
	
	SQLWCHAR SqlState[6];
	SQLINTEGER NativeError;
	SQLWCHAR Message[SQL_MAX_MESSAGE_LENGTH];
	SQLSMALLINT MessageLength;

	for (SQLSMALLINT RecIndex = 1; ; ++RecIndex)
	{
		const SQLRETURN Ret = ::SQLGetDiagRecW(
			HandleType, Handle, RecIndex,
			SqlState, &NativeError,
			Message, static_cast<SQLSMALLINT>(sizeof(Message) / sizeof(SQLWCHAR)), &MessageLength);

		if (Ret == SQL_NO_DATA || SQL_SUCCEEDED(Ret) == false)
			break;

		std::wcout << L"[DB][" << (Context ? Context : L"?") << L"] "
			<< L"SQLSTATE=" << SqlState
			<< L" Native=" << NativeError
			<< L" Msg=" << Message << L"\n";
	}
}
