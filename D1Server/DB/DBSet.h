#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBOrm.h"
#include "DB/DBQuery.h"
#include "DB/DBQueryBuildSql.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

class DBConnection;

/** EF Core 와 1:1 대응. Added 도 Identified 맵에 수용 (앱 발급 PK 전제). */
enum class EEntityState : uint8
{
	Unchanged = 0,  // Find 로 로드된 직후 상태. DetectChanges 가 Modified 로 승격할 수 있다.
	Added,          // Add 결과. SaveChanges 시 INSERT 대상. Snapshot 없음.
	Modified,       // Update 명시 호출 또는 DetectChanges 결과. SaveChanges 시 UPDATE 대상.
	Deleted,        // Remove 결과. SaveChanges 시 DELETE 대상.
};

/** DBSet<T> 트래킹 엔트리 1건 — 엔티티 shared_ptr + 상태 + ColumnMeta 영역 바이트 스냅샷. */
template<typename T>
struct DBSetEntry
{
	std::shared_ptr<T> Entity;
	EEntityState State = EEntityState::Unchanged;
	// ColumnMeta.Offset/Size 영역만 채운 sizeof(T) 바이트 버퍼. 빈 vector = snapshot 없음(Add/Update 경로).
	std::vector<uint8> Snapshot;
};

/** 타입 소거 베이스 — DBContext::Sets 에 unique_ptr<IDBSet> 로 보관되어 SaveChanges 가 일괄 FlushChanges 호출. */
class IDBSet
{
public:
	virtual ~IDBSet() = default;

	/** BEGIN 직후 호출 — Added INSERT → Modified UPDATE → Deleted DELETE 순 발행. 실패 시 상위가 Rollback. */
	virtual bool FlushChanges(DBConnection& Conn) = 0;
};

/** EF Core DbSet<T> 등가 — IdentityMap + ChangeTracker 역할. 단일 DB 워커 전제로 내부 동기화 없음. */
template<typename T>
class DBSet : public IDBSet
{
public:
	using PkType = TableMetadata<T>::PkType;
	using Entry = DBSetEntry<T>;

	DBSet(DBConnection& InConn) : Conn(InConn) {}

	DBSet(const DBSet&) = delete;
	DBSet& operator=(const DBSet&) = delete;
	DBSet(DBSet&&) = delete;
	DBSet& operator=(DBSet&&) = delete;

	/** 트래킹 Find — Identified HIT 면 ptr 반환(Deleted 는 nullptr), MISS 면 DB 로드 후 Unchanged+Snapshot 등록. */
	T* Find(const PkType& PkValue)
	{
		// 1. identity Map에서 캐싱 확인
		if (auto It = Identified.find(PkValue); It != Identified.end())
		{
			if (It->second.State == EEntityState::Deleted)
			{
				return nullptr;
			}
			return It->second.Entity.get();
		}

		// 2. 캐시 없으면 생성
		auto Entity = std::make_shared<T>();
		
		// 여기서는 바로 execute까지 진행함으로 트랜젝션 영역이 또 find만의 트랜젝션으로 진행됨
		// TODO : 현재 DB 워커 스레드의 갯수가 1개이기 때문에 가능한 execute. 멀티스레딩 환경에서는 수정 필요 
		if (DBOrm::Find<T>(Conn, PkValue, *Entity) == false)
		{
			return nullptr;
		}

		Entry NewEntry;
		NewEntry.Entity = Entity;
		NewEntry.State = EEntityState::Unchanged;
		CaptureSnapshot(*Entity, NewEntry.Snapshot);
		T* Raw = NewEntry.Entity.get();
		Identified.emplace(PkValue, std::move(NewEntry));
		return Raw;
	}

	/** 새 엔티티를 Added 로 등록(앱 발급 PK 전제). 중복 PK 면 nullptr. SaveChanges 에서 INSERT. */
	T* Add(std::shared_ptr<T> Entity)
	{
		if (Entity == nullptr) return nullptr;
		const PkType Pk = ExtractPk(*Entity);
		if (Identified.find(Pk) != Identified.end()) return nullptr;

		Entry NewEntry;
		NewEntry.Entity = std::move(Entity);
		NewEntry.State = EEntityState::Added;
		T* Raw = NewEntry.Entity.get();
		Identified.emplace(Pk, std::move(NewEntry));
		return Raw;
	}

	/** 외부 생성 엔티티를 Modified 로 직결 등록(AsNoTracking 경로). 기존 PK 있으면 false. 전체 비-PK UPDATE. */
	bool Update(std::shared_ptr<T> Entity)
	{
		if (Entity == nullptr) return false;
		const PkType Pk = ExtractPk(*Entity);
		if (Identified.find(Pk) != Identified.end()) return false;

		Entry NewEntry;
		NewEntry.Entity = std::move(Entity);
		NewEntry.State = EEntityState::Modified;
		Identified.emplace(Pk, std::move(NewEntry));
		return true;
	}

	/** tracked 엔티티를 Deleted 로 전환. Added 상태 Remove 는 맵에서 erase (Add 취소). */
	bool Remove(T* Entity)
	{
		if (Entity == nullptr) return false;
		const PkType Pk = ExtractPk(*Entity);
		auto It = Identified.find(Pk);
		if (It == Identified.end()) return false;

		if (It->second.State == EEntityState::Added)
		{
			Identified.erase(It);
			return true;
		}
		It->second.State = EEntityState::Deleted;
		return true;
	}

	/** Where 체인 시작 — ExprNode 술어를 담은 IQueryable<T> 를 반환. */
	IQueryable<T> Where(ExprNode Predicate)
	{
		IQueryable<T> Query(*this);
		Query.Where(std::move(Predicate));
		return Query;
	}

	/** OrderBy 체인 시작 — 첫 정렬 키를 오름차순으로 등록한 IQueryable<T> 반환. */
	template<size_t Idx>
	IQueryable<T> OrderBy(ColumnProxy<T, Idx> Column)
	{
		IQueryable<T> Query(*this);
		Query.OrderBy(Column);
		return Query;
	}

	/** OrderByDescending 체인 시작 — 첫 정렬 키를 내림차순으로 등록한 IQueryable<T> 반환. */
	template<size_t Idx>
	IQueryable<T> OrderByDescending(ColumnProxy<T, Idx> Column)
	{
		IQueryable<T> Query(*this);
		Query.OrderByDescending(Column);
		return Query;
	}

	/** AsNoTracking 체인 시작 — 추적 해제 플래그를 켠 IQueryable<T> 반환. */
	IQueryable<T> AsNoTracking()
	{
		IQueryable<T> Query(*this);
		Query.AsNoTracking();
		return Query;
	}

	/** 전체 행 카운트. SELECT COUNT(*) FROM T; */
	int64 Count()
	{
		IQueryable<T> Query(*this);
		return Query.Count();
	}

	/** 필터 카운트. SELECT COUNT(*) FROM T WHERE <pred>; */
	int64 Count(ExprNode Predicate)
	{
		IQueryable<T> Query(*this);
		Query.Where(std::move(Predicate));
		return Query.Count();
	}

	/** 전체 행 합계. SELECT SUM(Col) FROM T; 반환 타입은 컬럼 타입 따라 int64 or double. */
	template<size_t Idx>
	auto Sum(ColumnProxy<T, Idx> Column)
	{
		IQueryable<T> Query(*this);
		return Query.Sum(Column);
	}

	/** 필터 합계. SELECT SUM(Col) FROM T WHERE <pred>; */
	template<size_t Idx>
	auto Sum(ColumnProxy<T, Idx> Column, ExprNode Predicate)
	{
		IQueryable<T> Query(*this);
		Query.Where(std::move(Predicate));
		return Query.Sum(Column);
	}

	// ─── M7 내부 적재 헬퍼 (IQueryable<T> 종결자가 호출) ─────────

	/** Tracking 경로 — 동일 PK 존재 시 identity preservation, 신규면 Unchanged+Snapshot 등록. Deleted 히트 시 nullptr. */
	T* IngestRowWithTracking(std::shared_ptr<T> Row)
	{
		if (Row == nullptr)
		{
			return nullptr;
		}
		
		const PkType Pk = ExtractPk(*Row);
		if (auto It = Identified.find(Pk); It != Identified.end())
		{
			if (It->second.State == EEntityState::Deleted)
			{
				return nullptr;
			}
			return It->second.Entity.get();
		}

		Entry NewEntry;
		NewEntry.Entity = Row;
		NewEntry.State = EEntityState::Unchanged;
		CaptureSnapshot(*Row, NewEntry.Snapshot);
		T* Raw = NewEntry.Entity.get();
		Identified.emplace(Pk, std::move(NewEntry));
		return Raw;
	}

	/** NoTracking 경로 — NoTrackingResults 벡터에 shared_ptr 소유, Identified 는 건드리지 않음. */
	T* IngestRowNoTracking(std::shared_ptr<T> Row)
	{
		if (Row == nullptr)
		{
			return nullptr;
		}
		T* Raw = Row.get();
		NoTrackingResults.push_back(std::move(Row));
		return Raw;
	}

	/** DBQueryBuildSql 루틴이 SQL 실행 시 DBConnection 접근에 사용. */
	DBConnection& GetConnection() const { return Conn; }

	/** SaveChanges 디스패치 — DetectChanges → Added INSERT → Modified UPDATE → Deleted DELETE. 실패 시 false. */
	virtual bool FlushChanges(DBConnection& InConn) override
	{
		for (auto& Pair : Identified)
		{
			Entry& E = Pair.second;
			if (E.State != EEntityState::Unchanged)
				continue;
			if (E.Snapshot.empty())
				continue;
			// 만약 find로 가져와 수정했다면 State 반영이 안됐을 것 이므로
			if (IsDirty(*E.Entity, E.Snapshot))
				E.State = EEntityState::Modified;
		}

		for (auto& Pair : Identified)
		{
			if (Pair.second.State != EEntityState::Added)
				continue;
			if (DBOrm::Insert<T>(InConn, *Pair.second.Entity) == false)
				return false;
		}

		for (auto& Pair : Identified)
		{
			if (Pair.second.State != EEntityState::Modified)
				continue;
			if (DBOrm::Update<T>(InConn, *Pair.second.Entity) == false)
				return false;
		}

		for (auto& Pair : Identified)
		{
			if (Pair.second.State != EEntityState::Deleted)
				continue;
			if (DBOrm::Delete<T>(InConn, Pair.first) == false)
				return false;
		}

		return true;
	}

private:
	/** ColumnMeta 영역만 복사해 sizeof(T) 버퍼에 기록. 런타임 필드 위치는 0 으로 남는다 — non-POD 안전. */
	static void CaptureSnapshot(const T& Entity, std::vector<uint8>& OutSnapshot)
	{
		OutSnapshot.assign(sizeof(T), 0);
		const auto& Meta = GetTableMetadata<T>();
		const uint8* Src = reinterpret_cast<const uint8*>(&Entity);
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			const auto& Col = Meta.Columns[i];
			std::memcpy(OutSnapshot.data() + Col.Offset, Src + Col.Offset, Col.Size);
			if (Col.bNullable)
			{
				std::memcpy(OutSnapshot.data() + Col.IndicatorOffset, Src + Col.IndicatorOffset, sizeof(SQLLEN));
			}
		}
	}

	/** Snapshot 대비 현재 엔티티가 어느 컬럼이라도 달라졌는지 판정. */
	static bool IsDirty(const T& Entity, const std::vector<uint8>& Snapshot)
	{
		const auto& Meta = GetTableMetadata<T>();
		const uint8* Cur = reinterpret_cast<const uint8*>(&Entity);
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			const auto& Col = Meta.Columns[i];
			if (std::memcmp(Cur + Col.Offset, Snapshot.data() + Col.Offset, Col.Size) != 0)
			{
				return true;
			}
			if (Col.bNullable)
			{
				if (std::memcmp(Cur + Col.IndicatorOffset, Snapshot.data() + Col.IndicatorOffset, sizeof(SQLLEN)) != 0)
					return true;
			}
		}
		return false;
	}

	/** 엔티티의 PK 필드를 PkType 값으로 추출. BIGINT/VARCHAR/NVARCHAR 3종을 컴파일타임 분기한다. */
	static PkType ExtractPk(const T& Entity)
	{
		const auto& Meta = GetTableMetadata<T>();
		const auto& PkCol = Meta.Columns[Meta.PkIndex];
		const uint8* Src = reinterpret_cast<const uint8*>(&Entity) + PkCol.Offset;
		if constexpr (std::is_same_v<PkType, uint64>)
		{
			int64 V = 0;
			std::memcpy(&V, Src, sizeof(int64));
			return static_cast<uint64>(V);
		}
		else if constexpr (std::is_same_v<PkType, std::string>)
		{
			return std::string(reinterpret_cast<const char*>(Src));
		}
		else if constexpr (std::is_same_v<PkType, std::wstring>)
		{
			return std::wstring(reinterpret_cast<const wchar_t*>(Src));
		}
		else
		{
			static_assert(sizeof(T) == 0, "ExtractPk: unsupported PkType — add branch (uint64/string/wstring)");
			return PkType{};
		}
	}

	DBConnection& Conn;
	std::unordered_map<PkType, Entry> Identified;
	// M7 AsNoTracking 경로 결과 보관소. T* 가 DBContext 수명 동안 유효하도록 shared_ptr 로 소유.
	std::vector<std::shared_ptr<T>> NoTrackingResults;
};


// IQueryable<T> 종결자 정의 — BuildSelectSql → GetOrPrepare → Bind → Execute → Fetch 루프 → Ingest.

template<typename T>
inline std::vector<T*> IQueryable<T>::ToList()
{
	// 1. SQL + 바인딩 조립. TopLimit = 0 → SELECT 전수.
	std::wstring Sql;
	std::vector<QueryParameterBinding> Bindings;
	BuildSelectSql<T>(GetRootPredicate(), GetOrderTerms(), 0, Sql, Bindings);

	DBConnection& Conn = SourceSet.GetConnection();
	DBStatement* Stmt = Conn.GetOrPrepare(Sql);
	if (Stmt == nullptr) 
		return {};
	if (Stmt->Reset() == false) 
		return {};
	if (BindLiteralsToStatement(*Stmt, Bindings) == false) 
		return {};

	// 2. 결과 컬럼을 scratch 엔티티에 BindCol. Fetch 루프에서 scratch 가 매 row 로 덮어써짐.
	auto Scratch = std::make_shared<T>();
	const auto& Meta = GetTableMetadata<T>();
	void* EntityBase = static_cast<void*>(Scratch.get());
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindColFromMeta(*Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], EntityBase) == false)
			return {};
	}

	if (Stmt->Execute() == false) 
		return {};

	// 3. Fetch 루프 — 각 row 를 scratch 복사해 IdentityMap/NoTrackingCache 에 적재.
	std::vector<T*> Results;
	while (Stmt->Fetch())
	{
		auto Row = std::make_shared<T>(*Scratch);
		
		T* Tracked = IsNoTracking() ? SourceSet.IngestRowNoTracking(std::move(Row)) : SourceSet.IngestRowWithTracking(std::move(Row));
		
		if (Tracked != nullptr)
		{
			Results.push_back(Tracked);
		}
	}
	return Results;
}

template<typename T>
inline T* IQueryable<T>::FirstOrDefault()
{
	// TopLimit = 1 — SQL 서버가 1 행만 내려주므로 Fetch 1 회면 충분.
	std::wstring Sql;
	std::vector<QueryParameterBinding> Bindings;
	BuildSelectSql<T>(GetRootPredicate(), GetOrderTerms(), 1, Sql, Bindings);

	DBConnection& Conn = SourceSet.GetConnection();
	DBStatement* Stmt = Conn.GetOrPrepare(Sql);
	if (Stmt == nullptr) return nullptr;
	if (Stmt->Reset() == false) return nullptr;
	if (BindLiteralsToStatement(*Stmt, Bindings) == false) return nullptr;

	auto Scratch = std::make_shared<T>();
	const auto& Meta = GetTableMetadata<T>();
	void* EntityBase = static_cast<void*>(Scratch.get());
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindColFromMeta(*Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], EntityBase) == false)
			return nullptr;
	}

	if (Stmt->Execute() == false)
	{
		return nullptr;
	}
	
	// 0 행 — 매칭 없음.
	if (Stmt->Fetch() == false)
	{
		return nullptr;
	}

	auto Row = std::make_shared<T>(*Scratch);
	return IsNoTracking() ? SourceSet.IngestRowNoTracking(std::move(Row)) : SourceSet.IngestRowWithTracking(std::move(Row));
}

template<typename T>
inline T* IQueryable<T>::SingleOrDefault()
{
	// TopLimit = 2 — 2 행 여부를 알아야 하므로 1 이상이면 안 됨. 2 로 끊어 조기 탐지.
	std::wstring Sql;
	std::vector<QueryParameterBinding> Bindings;
	BuildSelectSql<T>(GetRootPredicate(), GetOrderTerms(), 2, Sql, Bindings);

	DBConnection& Conn = SourceSet.GetConnection();
	DBStatement* Stmt = Conn.GetOrPrepare(Sql);
	if (Stmt == nullptr)
	{
		return nullptr;
	}
	if (Stmt->Reset() == false)
	{
		return nullptr;
	}
	if (BindLiteralsToStatement(*Stmt, Bindings) == false)
	{
		return nullptr;
	}

	auto Scratch = std::make_shared<T>();
	const auto& Meta = GetTableMetadata<T>();
	void* EntityBase = static_cast<void*>(Scratch.get());
	for (size_t i = 0; i < Meta.NumColumns; ++i)
	{
		if (DBOrmDetail::BindColFromMeta(*Stmt, static_cast<SQLUSMALLINT>(i + 1), Meta.Columns[i], EntityBase) == false)
			return nullptr;
	}

	if (Stmt->Execute() == false)
	{
		return nullptr;
	}
	
	// 0 행 — 매칭 없음.
	if (Stmt->Fetch() == false)
	{
		return nullptr;
	}

	auto FirstRow = std::make_shared<T>(*Scratch);

	// 2 번째 Fetch 가 성공하면 violation — EF Core 는 예외, D1Server 는 로그 + nullptr. 이 부분이 FirstOrDefault와 다른점
	if (Stmt->Fetch())
	{
		std::cout << "[DBQuery] SingleOrDefault violation — query returned more than 1 row (Table="	<< GetTableMetadata<T>().TableName << ")" << std::endl;
		return nullptr;
	}

	return IsNoTracking() ? SourceSet.IngestRowNoTracking(std::move(FirstRow)) : SourceSet.IngestRowWithTracking(std::move(FirstRow));
}


// M7.1 집계 종결자 — 단일 스칼라 Fetch, Scratch/BindCol 불필요, Identity Map 무관.

template<typename T>
inline int64 IQueryable<T>::Count()
{
	std::wstring Sql;
	std::vector<QueryParameterBinding> Bindings;
	BuildCountSql<T>(GetRootPredicate(), Sql, Bindings);

	DBConnection& Conn = SourceSet.GetConnection();
	DBStatement* Stmt = Conn.GetOrPrepare(Sql);
	if (Stmt == nullptr)
	{
		return 0;
	}
	if (Stmt->Reset() == false)
	{
		return 0;
	}
	if (BindLiteralsToStatement(*Stmt, Bindings) == false)
	{
		return 0;
	}
	if (Stmt->Execute() == false)
	{
		return 0;
	}
	if (Stmt->Fetch() == false)
	{
		return 0;
	}

	int64 Result = 0;
	Stmt->GetColumnInt64(1, Result);
	return Result;
}

template<typename T>
template<size_t Idx>
inline auto IQueryable<T>::Sum(ColumnProxy<T, Idx>)
{
	// 컴파일타임 타입 가드 — 허용 안 되는 컬럼(문자열/날짜/바이너리)은 즉시 에러.
	constexpr ESqlCType CT = TableMetadata<T>::Columns[Idx].CType;
	static_assert(
		CT == ESqlCType::SBigInt || CT == ESqlCType::SLong ||
		CT == ESqlCType::SShort || CT == ESqlCType::UTinyInt ||
		CT == ESqlCType::Bit ||
		CT == ESqlCType::Float || CT == ESqlCType::Double,
		"IQueryable::Sum requires a numeric column (SBigInt/SLong/SShort/UTinyInt/Bit/Float/Double)");

	std::wstring Sql;
	std::vector<QueryParameterBinding> Bindings;
	BuildSumSql<T>(Idx, GetRootPredicate(), Sql, Bindings);

	DBConnection& Conn = SourceSet.GetConnection();
	DBStatement* Stmt = Conn.GetOrPrepare(Sql);

	if constexpr (CT == ESqlCType::Float || CT == ESqlCType::Double)
	{
		double Result = 0.0;
		if (Stmt == nullptr)
		{
			return Result;
		}
		if (Stmt->Reset() == false)
		{
			return Result;
		}
		if (BindLiteralsToStatement(*Stmt, Bindings) == false)
		{
			return Result;
		}
		if (Stmt->Execute() == false)
		{
			return Result;
		}
		if (Stmt->Fetch() == false)
		{
			return Result;   // 0 행 → SUM NULL → GetColumnDouble 이 0.0 정규화.
		}
		Stmt->GetColumnDouble(1, Result);
		return Result;
	}
	else
	{
		int64 Result = 0;
		if (Stmt == nullptr)
		{
			return Result;
		}
		if (Stmt->Reset() == false)
		{
			return Result;
		}
		if (BindLiteralsToStatement(*Stmt, Bindings) == false)
		{
			return Result;
		}
		if (Stmt->Execute() == false)
		{
			return Result;
		}
		if (Stmt->Fetch() == false)
		{
			return Result;   // 0 행 → SUM NULL → GetColumnInt64 가 0 정규화.
		}
		Stmt->GetColumnInt64(1, Result);
		return Result;
	}
}