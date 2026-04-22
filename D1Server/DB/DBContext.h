#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBSet.h"

#include <memory>
#include <typeindex>
#include <unordered_map>

class DBConnection;

/**
 * EF Core 의 DbContext 등가 — DB 작업의 사용자 진입점.
 * DBJobQueue::Schedule 람다 안에서 외부 Conn 인계 ctor 로 만들어 사용한다.
 *  - Pool 에서 Conn 을 빌리지 않는다 (이미 Schedule 이 빌렸음).
 *  - dtor 에서 Push 도 하지 않는다 (Schedule 이 끝낼 때 push).
 *
 * 본 마일스톤 시점의 DBContext 는 type_index → DBSet<T> 캐시 한 장이 전부. M7 에서
 * Transaction, M8 에서 Identity Map 의 통합 지점, M9 에서 SaveChanges 진입점이
 * 들어올 자리이다.
 *
 * 수명: 작업 1건 = DBContext 1건 (EF 의 short-lived DbContext 가이드와 일치).
 * Schedule 람다 종료 시점에 Sets 캐시도 함께 소멸하므로 DBSet<T> 참조를 람다 밖으로
 * 들고 나가지 말 것.
 */
class DBContext
{
public:
	explicit DBContext(DBConnection& InConn) : Conn(InConn) {}

	DBContext(const DBContext&) = delete;
	DBContext& operator=(const DBContext&) = delete;
	DBContext(DBContext&&) = delete;
	DBContext& operator=(DBContext&&) = delete;

	/** 타입별 DBSet<T> 를 lazy-create + 캐시해 반환. 동일 DBContext 내 호출은 같은 인스턴스 보장. */
	template<typename T>
	DBSet<T>& Set();

	DBConnection& GetConnection() const { return Conn; }

private:
	DBConnection& Conn;
	std::unordered_map<std::type_index, std::unique_ptr<IDBSet>> Sets;
};

template<typename T>
DBSet<T>& DBContext::Set()
{
	const std::type_index Idx(typeid(T));
	if (auto It = Sets.find(Idx); It != Sets.end())
		return *static_cast<DBSet<T>*>(It->second.get());

	auto Connection = std::make_unique<DBSet<T>>(Conn);
	DBSet<T>* Raw = Connection.get();
	Sets.emplace(Idx, std::move(Connection));
	return *Raw;
}