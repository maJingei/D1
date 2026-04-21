#pragma once

#include "Core/CoreMinimal.h"

#include <sql.h>
#include <sqlext.h>

/**
 * ODBC 연결(SQL_HANDLE_DBC) 하나를 감싸는 RAII 래퍼.
 * DBConnectionPool 이 HENV 와 함께 생성·수명 관리한다. execute 내부에서는
 * DBStatement 를 일회성 RAII 스코프 객체로 만들어 SQLExecDirectW 를 호출한다.
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

	/** 실패 시 SQLGetDiagRecW 루프로 SQLSTATE·네이티브 에러·메시지를 로그에 남긴다. */
	static void HandleError(SQLHANDLE Handle, SQLSMALLINT HandleType, const wchar_t* Context);

private:
	SQLHDBC Hdbc = SQL_NULL_HDBC;
};