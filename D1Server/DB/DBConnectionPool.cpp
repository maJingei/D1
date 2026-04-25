#include "DB/DBConnectionPool.h"
#include "DB/DBConnection.h"

#include <iostream>

DBConnectionPool& DBConnectionPool::GetInstance()
{
	// GlobalJobQueue 와 동일 패턴: GetInstance 내부 static 포인터에 new 로 지연 생성.
	// 정적 소멸 순서 이슈를 피하고 프로세스 종료 시 leak 을 허용한다.
	static DBConnectionPool* Instance = new DBConnectionPool();
	return *Instance;
}

DBConnectionPool::~DBConnectionPool()
{
	// Leaked singleton 이므로 이 경로는 일반 실행에서 돌지 않는다. 방어적 정리만 수행.
	Shutdown();
}

bool DBConnectionPool::Init(const wchar_t* ConnectString, int32 PoolSize)
{
	std::lock_guard<std::mutex> Lock(Mutex);

	if (Henv != SQL_NULL_HENV)
	{
		std::cout << "[DBConnectionPool] Init called twice\n";
		return false;
	}

	// 1. 환경 핸들 할당 및 ODBC 3.8 버전 설정.
	SQLRETURN Ret = ::SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &Henv);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		std::cout << "[DBConnectionPool] SQLAllocHandle(ENV) failed\n";
		Henv = SQL_NULL_HENV;
		return false;
	}

	Ret = ::SQLSetEnvAttr(Henv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3_80), 0);
	if (SQL_SUCCEEDED(Ret) == false)
	{
		DBConnection::HandleError(Henv, SQL_HANDLE_ENV, L"SQLSetEnvAttr(ODBC3_80)");
		TeardownUnlocked();
		return false;
	}

	// 2. PoolSize 만큼 DBConnection 선생성 + connect.
	Connections.reserve(PoolSize);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		DBConnection* Conn = new DBConnection(Henv);
		if (Conn->GetHandle() == SQL_NULL_HDBC || Conn->Connect(ConnectString) == false)
		{
			delete Conn;
			// 이미 성공한 연결들도 유지하지 말고 전부 회수해 초기화 자체를 실패 처리한다.
			TeardownUnlocked();
			return false;
		}
		Connections.push_back(Conn);
	}
	return true;
}

void DBConnectionPool::Shutdown()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	TeardownUnlocked();
}

DBConnection* DBConnectionPool::Pop()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (Connections.empty())
		return nullptr;

	DBConnection* Conn = Connections.back();
	Connections.pop_back();
	return Conn;
}

void DBConnectionPool::Push(DBConnection* Connection)
{
	if (Connection == nullptr)
		return;

	std::lock_guard<std::mutex> Lock(Mutex);
	Connections.push_back(Connection);
}

void DBConnectionPool::TeardownUnlocked()
{
	for (DBConnection* Conn : Connections)
		delete Conn;
	Connections.clear();

	if (Henv != SQL_NULL_HENV)
	{
		::SQLFreeHandle(SQL_HANDLE_ENV, Henv);
		Henv = SQL_NULL_HENV;
	}
}