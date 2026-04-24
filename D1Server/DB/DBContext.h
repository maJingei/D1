#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBConnection.h"
#include "DB/DBSet.h"

#include <memory>
#include <typeindex>
#include <unordered_map>

/**
 * EF Core 의 DbContext 등가 — DB 작업의 사용자 진입점.
 * DBJobQueue::Schedule 람다 안에서 외부 Conn 인계 ctor 로 만들어 사용한다.
 *  - Pool 에서 Conn 을 빌리지 않는다 (이미 Schedule 이 빌렸음).
 *  - dtor 에서 Push 도 하지 않는다 (Schedule 이 끝낼 때 push).
 *
 * [M6 SaveChanges 모델]
 *  "1 Job = 1 DBContext = 1 SaveChanges = 1 Transaction" —
 *  SaveChanges 는 한 DBContext 인스턴스에서 **단 1회만** 허용된다. 성공·실패와 무관하게 호출 직후
 *  DBContext 는 dead 로 마킹되어 이후 모든 SaveChanges 호출이 false 반환. 재시도는 새 Job/새 DBContext.
 *  내부 절차:
 *    1) BeginTransaction
 *    2) 등록된 모든 DBSet 에 대해 FlushChanges 순차 호출(Added INSERT → Modified UPDATE → Deleted DELETE)
 *    3) 어느 단계든 false 면 즉시 RollbackTransaction 후 false 반환
 *    4) 전부 성공하면 CommitTransaction. Commit 이 실패해도 Rollback 명시 시도 후 false 반환.
 *
 * 수명: 작업 1건 = DBContext 1건 (EF 의 short-lived DbContext 가이드와 일치).
 * Schedule 람다 종료 시점에 Sets 캐시도 함께 소멸하므로 DBSet<T> 참조나 Find/Add 반환 ptr 을
 * 람다 밖으로 들고 나가지 말 것.
 */
class DBContext
{
public:
	DBContext(DBConnection& InConn) : Conn(InConn) {}

	DBContext(const DBContext&) = delete;
	DBContext& operator=(const DBContext&) = delete;
	DBContext(DBContext&&) = delete;
	DBContext& operator=(DBContext&&) = delete;

	/** 타입별 DBSet<T> 를 lazy-create + 캐시해 반환. 동일 DBContext 내 호출은 같은 인스턴스 보장. */
	template<typename T>
	DBSet<T>& Set();

	/**
	 * M6 진입점. 등록된 모든 DBSet 의 변경사항을 하나의 트랜잭션으로 일괄 반영.
	 * 성공 시 true, 실패 시 false — 실패 시 DB 는 BEGIN 직전 상태로 롤백, 메모리 쪽 복원은 하지 않음(DBContext 폐기 전제).
	 * 1회 한정 — 두 번째 호출은 dead 상태로 즉시 false.
	 */
	bool SaveChanges();

	DBConnection& GetConnection() const { return Conn; }

private:
	DBConnection& Conn;
	std::unordered_map<std::type_index, std::unique_ptr<IDBSet>> Sets;
	// SaveChanges 1회 한정 플래그. 첫 호출 직후 true 로 고정되고 이후 모든 SaveChanges 호출이 false.
	bool bDead = false;
};

template<typename T>
DBSet<T>& DBContext::Set()
{
	const std::type_index Idx(typeid(T));
	if (auto It = Sets.find(Idx); It != Sets.end())
		return *static_cast<DBSet<T>*>(It->second.get());

	auto NewSet = std::make_unique<DBSet<T>>(Conn);
	DBSet<T>* Raw = NewSet.get();
	Sets.emplace(Idx, std::move(NewSet));
	return *Raw;
}

inline bool DBContext::SaveChanges()
{
	if (bDead)
	    return false;
	// 성공이든 실패든 호출 직후 dead 로 고정 — 2회 호출 방지 + 실패 후 재시도 방지(재시도는 새 Job 에서).
	bDead = true;

	if (Conn.BeginTransaction() == false) 
	    return false;

	for (auto& Pair : Sets)
	{
		if (Pair.second->FlushChanges(Conn) == false)
		{
			Conn.RollbackTransaction();
			return false;
		}
	}

	if (Conn.CommitTransaction() == false)
	{
		// Commit 이 실패했다면 ODBC 상 이미 내부 롤백됐을 가능성이 높지만, autocommit 복원과 명시 의미를 위해 한 번 더 호출(idempotent).
		Conn.RollbackTransaction();
		return false;
	}

	return true;
}