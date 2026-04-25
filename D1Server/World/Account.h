#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBMeta.h"
#include "DB/DBQuery.h"

#include <sql.h>
#include <sqlext.h>

#include <string>

/** dbo.Account POD 스냅샷. Id 는 VARCHAR(32) natural key PK, PlayerID 는 PlayerEntry.PlayerID 논리 FK. */
struct Account
{
	char Id[33] = {};        // VARCHAR(32) + null-term
	char Password[33] = {};  // 평문 보안 non-goal.
	int64 PlayerID = 0;
};

DB_REGISTER_TABLE_BEGIN(Account, "dbo.Account", std::string)
	DB_COLUMN_PK(Id, VARCHAR(32))
	DB_COLUMN(Password, VARCHAR(32))
	DB_COLUMN(PlayerID, BIGINT)
DB_REGISTER_TABLE_END()

// LINQ ColumnProxy 묶음. 인덱스는 DB_REGISTER_TABLE_BEGIN 블록의 DB_COLUMN 선언 순서와 1:1.
struct AccountCol
{
	static constexpr ColumnProxy<Account, 0> Id{};
	static constexpr ColumnProxy<Account, 1> Password{};
	static constexpr ColumnProxy<Account, 2> PlayerID{};
};