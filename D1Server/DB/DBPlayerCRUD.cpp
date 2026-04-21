#include "DB/DBPlayerCRUD.h"

#include "DB/DBConnection.h"
#include "DB/DBStatement.h"
#include "World/Level.h"

#include <sstream>

bool DBPlayer::Insert(DBConnection& Conn, const PlayerEntry& Entry)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	// M2 는 파라미터 바인딩 없이 SQL 텍스트 인라인 조립. wostringstream 의 기본 숫자 포맷으로 충분하다
	// (INT/REAL 리터럴 → SQL Server 암묵 캐스트). SQLBindParameter 로의 승격은 M3+ 에서 진행.
	std::wostringstream Sql;
	Sql << L"INSERT INTO dbo.PlayerEntry "
		<< L"(PlayerID, CharacterType, LevelID, TileX, TileY, HP, MaxHP, AttackDamage, TileMoveSpeed) "
		<< L"VALUES ("
		<< Entry.PlayerID << L", "
		<< static_cast<int32>(Entry.CharacterType) << L", "
		<< Entry.LevelID << L", "
		<< Entry.TileX << L", "
		<< Entry.TileY << L", "
		<< Entry.HP << L", "
		<< Entry.MaxHP << L", "
		<< Entry.AttackDamage << L", "
		<< Entry.TileMoveSpeed
		<< L");";

	return Stmt.ExecuteDirect(Sql.str().c_str());
}

bool DBPlayer::Find(DBConnection& Conn, uint64 PlayerID, PlayerEntry& OutEntry)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	std::wostringstream Sql;
	Sql << L"SELECT CharacterType, LevelID, TileX, TileY, HP, MaxHP, AttackDamage, TileMoveSpeed "
		<< L"FROM dbo.PlayerEntry WHERE PlayerID = " << PlayerID << L";";

	if (Stmt.ExecuteDirect(Sql.str().c_str()) == false)
		return false;

	// Fetch false = SQL_NO_DATA(행 없음) 또는 페치 실패. 둘 다 "Find 실패" 로 호출자에게 반환.
	if (Stmt.Fetch() == false)
		return false;

	int32 CharacterType = 0;
	int32 LevelID = 0;
	int32 TileX = 0;
	int32 TileY = 0;
	int32 HP = 0;
	int32 MaxHP = 0;
	int32 AttackDamage = 0;
	float TileMoveSpeed = 0.f;

	// 컬럼 인덱스는 SELECT 리스트 순서(1-based). 하나라도 실패하면 부분 변경을 방지하기 위해 즉시 반환.
	if (Stmt.GetColumnInt32(1, CharacterType) == false) return false;
	if (Stmt.GetColumnInt32(2, LevelID) == false) return false;
	if (Stmt.GetColumnInt32(3, TileX) == false) return false;
	if (Stmt.GetColumnInt32(4, TileY) == false) return false;
	if (Stmt.GetColumnInt32(5, HP) == false) return false;
	if (Stmt.GetColumnInt32(6, MaxHP) == false) return false;
	if (Stmt.GetColumnInt32(7, AttackDamage) == false) return false;
	if (Stmt.GetColumnFloat(8, TileMoveSpeed) == false) return false;

	// 영속 9필드만 OutEntry 에 주입. LastDir / bIsDead / Session / LastAcceptedSeq /
	// bJustTeleported / LastMoveTimeMs 는 런타임 필드라 DB 매핑 대상이 아니며 건드리지 않는다.
	OutEntry.PlayerID = PlayerID;
	OutEntry.CharacterType = static_cast<Protocol::CharacterType>(CharacterType);
	OutEntry.LevelID = LevelID;
	OutEntry.TileX = TileX;
	OutEntry.TileY = TileY;
	OutEntry.HP = HP;
	OutEntry.MaxHP = MaxHP;
	OutEntry.AttackDamage = AttackDamage;
	OutEntry.TileMoveSpeed = TileMoveSpeed;
	return true;
}

bool DBPlayer::Update(DBConnection& Conn, const PlayerEntry& Entry)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	std::wostringstream Sql;
	Sql << L"UPDATE dbo.PlayerEntry SET "
		<< L"CharacterType = " << static_cast<int32>(Entry.CharacterType) << L", "
		<< L"LevelID = " << Entry.LevelID << L", "
		<< L"TileX = " << Entry.TileX << L", "
		<< L"TileY = " << Entry.TileY << L", "
		<< L"HP = " << Entry.HP << L", "
		<< L"MaxHP = " << Entry.MaxHP << L", "
		<< L"AttackDamage = " << Entry.AttackDamage << L", "
		<< L"TileMoveSpeed = " << Entry.TileMoveSpeed
		<< L" WHERE PlayerID = " << Entry.PlayerID << L";";

	return Stmt.ExecuteDirect(Sql.str().c_str());
}

bool DBPlayer::Delete(DBConnection& Conn, uint64 PlayerID)
{
	DBStatement Stmt(Conn.GetHandle());
	if (Stmt.IsValid() == false)
		return false;

	std::wostringstream Sql;
	Sql << L"DELETE FROM dbo.PlayerEntry WHERE PlayerID = " << PlayerID << L";";

	return Stmt.ExecuteDirect(Sql.str().c_str());
}