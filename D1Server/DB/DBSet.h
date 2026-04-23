#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBOrm.h"

#include <iostream>
#include <memory>
#include <unordered_map>

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
 * EF Core 의 DbSet<T> 와 같은 자리. M5 부터 IdentityMap 의 홈이다.
 *
 * [M5 IdentityMap 통합]
 *  - PkType 은 TableMetadata<T>::PkType 로부터 가져온다 (BIGINT=uint64 / VARCHAR=std::string / NVARCHAR=std::wstring).
 *  - IdentityMap: unordered_map<PkType, shared_ptr<T>>. 소유권은 맵이 갖고, Find 는 .get() 으로 non-owning ptr 만 노출.
 *  - Find 반환 규약: T* (nullptr = 행 없음). EF Core TEntity? Find(...) 의 C++ 직접 대응.
 *  - 호출자 수명 전제: DBContext 는 DBJobQueue::Schedule 람다 스코프 안에서만 살아있으므로
 *    반환된 T* 를 스코프 밖으로 들고 나가는 경로가 구조적으로 없다 → dangling 위험 차단.
 *  - 동기화 범위(M5 한정): Find 는 hit/miss 모두 맵에 반영, Delete 는 맵 entry 제거,
 *    Insert/Update 는 맵을 건드리지 않는다. M6 ChangeTracker 에서 Insert/Update 통합 예정.
 *  - Thread safety: DBContext 가 단일 DB 워커 스레드에서만 사용되므로 IdentityMap 자체 동기화 불필요.
 *
 * 사용자는 DBContext::Set<T>() 를 통해서만 인스턴스를 얻는 것이 원칙. ctor 는
 * public 이지만 외부에서 직접 만들 일 없음.
 */
template<typename T>
class DBSet : public IDBSet
{
public:
	using PkType = typename TableMetadata<T>::PkType;

	explicit DBSet(DBConnection& InConn) : Conn(InConn) {}

	DBSet(const DBSet&) = delete;
	DBSet& operator=(const DBSet&) = delete;
	DBSet(DBSet&&) = delete;
	DBSet& operator=(DBSet&&) = delete;

	/** Insert — 즉시 실행. M5 범위에서는 IdentityMap 에 반영하지 않는다 (M6 ChangeTracker 에서 통합). */
	bool Insert(const T& Entity) { return DBOrm::Insert<T>(Conn, Entity); }

	/** Update — 즉시 실행. M5 범위에서는 IdentityMap 을 건드리지 않는다. */
	bool Update(const T& Entity) { return DBOrm::Update<T>(Conn, Entity); }

	/**
	 * M5 IdentityMap 경유 1행 조회.
	 *   hit  : 캐시된 엔티티의 non-owning ptr 즉시 반환, DB 왕복 0회, [DBIdMap] HIT 로그.
	 *   miss : DBOrm::Find 로 DB 에서 읽어 shared_ptr<T> 로 포장 후 맵에 저장, [DBIdMap] MISS 로그.
	 *   row 없음: nullptr 반환 — 음성 캐싱 없음(맵에 저장 안 함).
	 */
	T* Find(const PkType& PkValue)
	{
		// 1. 맵에서 먼저 조회. hit 면 DB 왕복 0회로 non-owning ptr 바로 반환.
		if (auto It = IdentityMap.find(PkValue); It != IdentityMap.end())
		{
			std::cout << "[DBIdMap] HIT  (Table=" << GetTableMetadata<T>().TableName << ", PK=" << PkValue << ")" << std::endl;
			return It->second.get();
		}

		// 2. miss — DB 로 내려가 1행 SELECT. 기존 DBOrm::Find (OUT T&) 를 그대로 재사용.
		std::cout << "[DBIdMap] MISS (Table=" << GetTableMetadata<T>().TableName << ", PK=" << PkValue << ")" << std::endl;
		auto Entity = std::make_shared<T>();
		if (DBOrm::Find<T>(Conn, PkValue, *Entity) == false) return nullptr;

		// 3. DB 성공 시에만 맵에 저장. row 없음(Fetch false) 은 위에서 이미 nullptr 반환으로 종료 — 음성 캐싱 없음.
		T* Raw = Entity.get();
		IdentityMap.emplace(PkValue, std::move(Entity));
		return Raw;
	}

	/**
	 * M5 IdentityMap 일관성 유지용 Delete.
	 * DB 삭제 결과와 무관하게 맵 entry 는 항상 제거한다 — DB 실패 시 stale entry 가 남는 것이 더 위험하므로.
	 */
	bool Delete(const PkType& PkValue)
	{
		const bool bOk = DBOrm::Delete<T>(Conn, PkValue);
		IdentityMap.erase(PkValue);
		return bOk;
	}

private:
	DBConnection& Conn;
	std::unordered_map<PkType, std::shared_ptr<T>> IdentityMap;
};