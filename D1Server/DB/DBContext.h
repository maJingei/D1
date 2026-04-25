#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBConnection.h"
#include "DB/DBSet.h"

#include <memory>
#include <typeindex>
#include <unordered_map>

/** EF Core DbContext 등가 — "1 Job = 1 DBContext = 1 SaveChanges = 1 Transaction". Schedule 람다 스코프 수명. */
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

	/** 등록된 모든 DBSet 변경사항을 한 트랜잭션으로 일괄 반영. 1회 한정 — 2회 호출부터 false. */
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