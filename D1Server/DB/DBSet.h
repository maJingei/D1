#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBOrm.h"

class DBConnection;

/**
 * 타입 소거 베이스. DBContext 의 캐시 컨테이너가 unique_ptr<IDBSet> 으로 보관하기 위함.
 * 가상 dtor 만 갖는다 — 실제 다형성 호출 경로는 없음.
 */
class IDBSet
{
public:
	virtual ~IDBSet() = default;
};

/**
 * EF Core 의 DbSet<T> 와 같은 자리. 본 마일스톤(DBContext 리네임) 시점에는
 * DBOrm::Xxx<T> 로 위임만 하는 상태 없는 객체 — 멤버는 DBConnection& 한 개.
 * M8 에서 Identity Map 이 들어오면 본 클래스가 PK → shared_ptr<T> 맵의 홈이 된다.
 *
 * 사용자는 DBContext::Set<T>() 를 통해서만 인스턴스를 얻는 것이 원칙. ctor 는
 * public 이지만 외부에서 직접 만들 일 없음.
 */
template<typename T>
class DBSet : public IDBSet
{
public:
	explicit DBSet(DBConnection& InConn) : Conn(InConn) {}

	DBSet(const DBSet&) = delete;
	DBSet& operator=(const DBSet&) = delete;
	DBSet(DBSet&&) = delete;
	DBSet& operator=(DBSet&&) = delete;

	bool Insert(const T& Row)              { return DBOrm::Insert<T>(Conn, Row); }
	bool Find(uint64 PkValue, OUT T& Out)  { return DBOrm::Find<T>(Conn, PkValue, Out); }
	bool Update(const T& Row)              { return DBOrm::Update<T>(Conn, Row); }
	bool Delete(uint64 PkValue)            { return DBOrm::Delete<T>(Conn, PkValue); }

private:
	DBConnection& Conn;
};