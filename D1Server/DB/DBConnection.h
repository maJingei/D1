#pragma once

#include "Core/CoreMinimal.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <sql.h>
#include <sqlext.h>

class DBStatement;

/**
 * ODBC 연결(SQL_HANDLE_DBC) 하나를 감싸는 RAII 래퍼.
 * DBConnectionPool 이 HENV 와 함께 생성·수명 관리한다. execute 내부에서는
 * DBStatement 를 일회성 RAII 스코프 객체로 만들어 SQLExecDirectW 를 호출한다.
 *
 * Statement Cache: SQL 텍스트별로 prepared DBStatement 를 캐시한다. DBJobQueue 가
 * 한 잡 동안 connection 을 독점하는 모델이므로 락 없이 동시성 안전 — 다른 워커가
 * 같은 connection 을 동시에 만지지 않는다. 캐시 항목의 수명은 DBConnection 과 동일.
 */
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

	/** 연결을 끊고 HDBC 를 해제한다. 소멸자에서도 호출된다. */
	void Disconnect();

	SQLHDBC GetHandle() const { return Hdbc; }

	/**
	 * SQL 텍스트로 prepared DBStatement 를 조회·반환. 캐시에 없으면 lazy 하게 alloc + Prepare 후 캐시.
	 * 반환된 statement 는 본 connection 소유 — 호출자는 Reset → Bind → Execute 만 수행하고 해제하지 않는다.
	 * Prepare 실패 시 nullptr.
	 */
	DBStatement* GetOrPrepare(const std::wstring& Sql);

	/** 실패 시 SQLGetDiagRecW 루프로 SQLSTATE·네이티브 에러·메시지를 로그에 남긴다. */
	static void HandleError(SQLHANDLE Handle, SQLSMALLINT HandleType, const wchar_t* Context);

private:
	SQLHDBC Hdbc = SQL_NULL_HDBC;

	// SQL 텍스트 → prepared statement. wstring 키 hash/compare 비용을 줄이려면 호출자가
	// 동일한 wstring 인스턴스(BuildXxxSql 의 function-local static)를 넘기는 것이 이상적.
	std::unordered_map<std::wstring, std::unique_ptr<DBStatement>> StmtCache;
};