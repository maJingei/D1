#pragma once

#include "Core/CoreMinimal.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <sql.h>
#include <sqlext.h>

class DBStatement;

/** SQL_HANDLE_DBC RAII 래퍼 — DBConnectionPool 이 수명 관리. Statement Cache 는 한 Job 독점 모델로 락 없음. */
class DBConnection
{
public:
	DBConnection(SQLHENV InHenv);
	~DBConnection();

	DBConnection(const DBConnection&) = delete;
	DBConnection& operator=(const DBConnection&) = delete;
	DBConnection(DBConnection&&) = delete;
	DBConnection& operator=(DBConnection&&) = delete;

	/** connectString 으로 MSSQL 에 연결한다. 성공 시 true. */
	bool Connect(const wchar_t* ConnectString);

	/** SQL 한 문장을 Execute 하고 성공 여부를 반환한다. (v1: INSERT 전용) */
	bool Execute(const wchar_t* Sql);

	/** 트랜잭션 3-API — autocommit OFF/ON 전환. Commit/Rollback 모두 autocommit ON 복원(풀 반환 전 상태 보장). */
	bool BeginTransaction();
	bool CommitTransaction();
	bool RollbackTransaction();

	/** 연결을 끊고 HDBC 를 해제한다. 소멸자에서도 호출된다. */
	void Disconnect();

	SQLHDBC GetHandle() const { return Hdbc; }

	/** SQL 텍스트로 prepared DBStatement 조회 — 캐시 miss 면 lazy alloc+Prepare. 소유권 보유, 실패 시 nullptr. */
	DBStatement* GetOrPrepare(const std::wstring& Sql);

	/** 실패 시 SQLGetDiagRecW 루프로 SQLSTATE·네이티브 에러·메시지를 로그에 남긴다. */
	static void HandleError(SQLHANDLE Handle, SQLSMALLINT HandleType, const wchar_t* Context);

private:
	SQLHDBC Hdbc = SQL_NULL_HDBC;

	// SQL 텍스트 → prepared statement. wstring 키 hash/compare 비용을 줄이려면 호출자가
	// 동일한 wstring 인스턴스(BuildXxxSql 의 function-local static)를 넘기는 것이 이상적.
	std::unordered_map<std::wstring, std::unique_ptr<DBStatement>> StmtCache;
};