#include "DB/DBConnection.h"
#include "DB/DBStatement.h"

#include <iostream>

#include "DBContext.h"

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
		(const_cast<wchar_t*>(ConnectString)),
		SQL_NTS,
		OutConnectionString,
		(sizeof(OutConnectionString) / sizeof(SQLWCHAR)),
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

bool DBConnection::BeginTransaction()
{
	if (Hdbc == SQL_NULL_HDBC)
		return false;

	// Autocommit 을 OFF 로 내리는 순간 이후 SQL 들이 한 트랜잭션으로 묶인다.
	const SQLRETURN Ret = ::SQLSetConnectAttr(Hdbc, SQL_ATTR_AUTOCOMMIT, (SQL_AUTOCOMMIT_OFF), SQL_IS_INTEGER);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		HandleError(Hdbc, SQL_HANDLE_DBC, L"BeginTransaction(SetAttr AUTOCOMMIT OFF)");
		return false;
	}
	return true;
}

bool DBConnection::CommitTransaction()
{
	if (Hdbc == SQL_NULL_HDBC)
		return false;

	const SQLRETURN Ret = ::SQLEndTran(SQL_HANDLE_DBC, Hdbc, SQL_COMMIT);
	
	if (SQL_SUCCEEDED(Ret) == false)
		HandleError(Hdbc, SQL_HANDLE_DBC, L"CommitTransaction(SQLEndTran COMMIT)");

	// 성공·실패와 무관하게 autocommit 복원 — connection 이 Pool 로 돌아갈 때 다음 Job 이 기본 모드를 기대한다.
	::SQLSetConnectAttr(Hdbc, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_INTEGER);
	return SQL_SUCCEEDED(Ret);
}

bool DBConnection::RollbackTransaction()
{
	if (Hdbc == SQL_NULL_HDBC)
		return false;

	const SQLRETURN Ret = ::SQLEndTran(SQL_HANDLE_DBC, Hdbc, SQL_ROLLBACK);
	if (SQL_SUCCEEDED(Ret) == false)
		HandleError(Hdbc, SQL_HANDLE_DBC, L"RollbackTransaction(SQLEndTran ROLLBACK)");

	::SQLSetConnectAttr(Hdbc, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), SQL_IS_INTEGER);
	return SQL_SUCCEEDED(Ret);
}

void DBConnection::Disconnect()
{
	if (Hdbc != SQL_NULL_HDBC)
	{
		// 캐시된 statement 들이 본 HDBC 의 자식 핸들이므로 SQLFreeHandle(DBC) 보다 먼저 비워야 한다.
		// 부모 핸들이 먼저 해제되면 자식의 SQLFreeHandle(STMT) 가 invalid handle 위에서 일어남.
		StmtCache.clear();

		// SQLDisconnect 는 이미 연결되어 있지 않아도 안전(상태 검사 후 성공 반환).
		::SQLDisconnect(Hdbc);
		::SQLFreeHandle(SQL_HANDLE_DBC, Hdbc);
		Hdbc = SQL_NULL_HDBC;
	}
}

DBStatement* DBConnection::GetOrPrepare(const std::wstring& Sql)
{
	if (Hdbc == SQL_NULL_HDBC)
		return nullptr;

    // 해당 sql에 대한 statement캐시 존재하면 반환 
	if (auto It = StmtCache.find(Sql); It != StmtCache.end())
		return It->second.get();
    
    // 없으면 unique 생성
	auto Stmt = std::make_unique<DBStatement>(Hdbc);
	if (Stmt->IsValid() == false)
		return nullptr;

    // 그리고 바로 prepare
	if (Stmt->Prepare(Sql.c_str()) == false)
		return nullptr;

	DBStatement* Raw = Stmt.get();
	StmtCache.emplace(Sql, std::move(Stmt));
	return Raw;
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
			Message, (sizeof(Message) / sizeof(SQLWCHAR)), &MessageLength);

		if (Ret == SQL_NO_DATA || SQL_SUCCEEDED(Ret) == false)
			break;

		std::wcout << L"[DB][" << (Context ? Context : L"?") << L"] "
			<< L"SQLSTATE=" << SqlState
			<< L" Native=" << NativeError
			<< L" Msg=" << Message << L"\n";
	}
}
