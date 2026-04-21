#pragma once

#include "Core/CoreMinimal.h"

#include <sql.h>
#include <sqlext.h>

#include <mutex>
#include <vector>

class DBConnection;

/**
 * ODBC 환경(SQL_HANDLE_ENV) 을 소유하고 DBConnection 풀을 관리하는 싱글톤.
 */
class DBConnectionPool
{
public:
	/** Leaked singleton: GetInstance 내부 static 포인터에 new(GlobalJobQueue 와 동일). ODBC 자원만 Shutdown() 으로 명시 해제한다. */
	static DBConnectionPool& GetInstance();

	DBConnectionPool(const DBConnectionPool&) = delete;
	DBConnectionPool& operator=(const DBConnectionPool&) = delete;
	DBConnectionPool(DBConnectionPool&&) = delete;
	DBConnectionPool& operator=(DBConnectionPool&&) = delete;

	/** HENV 를 할당하고 PoolSize 개의 DBConnection 을 미리 connect 까지 완료해 둔다. 실패 시 부분 생성된 리소스를 모두 회수하고 false 반환. */
	bool Init(const wchar_t* ConnectString, int32 PoolSize);

	/** ODBC 자원(HENV, HDBC N개) 을 명시적으로 회수한다. Engine.Destroy 에서 호출. 중복 호출 안전. */
	void Shutdown();

	/** 가용 연결 하나를 꺼내 반환한다. Idle 이 비어있으면(또는 Shutdown 후) nullptr. */
	DBConnection* Pop();

	/** 사용 완료한 연결을 풀에 되돌려놓는다. */
	void Push(DBConnection* Connection);

private:
	DBConnectionPool() = default;
	~DBConnectionPool();

	/** Init 실패 경로와 Shutdown 경로에서 공유. Idle 에 남아있는 연결과 HENV 를 회수한다. */
	void TeardownUnlocked();

	SQLHENV Henv = SQL_NULL_HENV;

	std::mutex Mutex;
	std::vector<DBConnection*> Idle;
};