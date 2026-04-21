#pragma once

#include "Core/CoreMinimal.h"

class DBConnection;
struct PlayerEntry;

/**
 * dbo.PlayerEntry 테이블에 대한 수동 CRUD (M2 단계).
 * M3 이후 매크로 기반 메타데이터로 자동 생성으로 대체될 예정. 본 파일의 의도는 자동 생성
 * 이전에 보일러플레이트의 모양을 손으로 한 번 직접 체감하는 것이다.
 *
 * v1 제약: 파라미터 바인딩 없이 SQL 텍스트 인라인 조립. SQLBindParameter 는 M3+ 범위.
 */
class DBPlayer
{
public:
	/** 단일 PlayerEntry 를 dbo.PlayerEntry 에 삽입. 성공 시 true. PK 중복 등으로 실패하면 false. */
	static bool Insert(DBConnection& Conn, const PlayerEntry& Entry);

	/** PlayerID 로 dbo.PlayerEntry 1행을 읽어 OutEntry 의 영속 9필드를 채운다. 행 없음/에러 시 false. */
	static bool Find(DBConnection& Conn, uint64 PlayerID, OUT PlayerEntry& OutEntry);

	/** Entry.PlayerID 로 식별된 행의 영속 8컬럼(PK 제외) 을 Entry 값으로 갱신. 0행이어도 ODBC 가 성공으로 반환. */
	static bool Update(DBConnection& Conn, const PlayerEntry& Entry);

	/** PlayerID 로 1행 삭제. 0행이어도 성공으로 반환. */
	static bool Delete(DBConnection& Conn, uint64 PlayerID);
};