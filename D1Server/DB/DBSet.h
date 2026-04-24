#pragma once

#include "Core/CoreMinimal.h"
#include "DB/DBOrm.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

class DBConnection;

/**
 * M6 EntityState — EF Core 와 1:1 대응.
 * M6 시점의 D1Server 는 모든 엔티티가 애플리케이션 발급 PK 를 가지므로 Added 상태도 Identified 맵에 그대로 들어간다.
 * IDENTITY/서버 생성 PK 지원(M8) 시점에 PendingAdds 2-버킷 구조로 분리 예정 — techspec 3.5.1 참조.
 */
enum class EEntityState : uint8
{
	Unchanged = 0,  // Find 로 로드된 직후 상태. DetectChanges 가 Modified 로 승격할 수 있다.
	Added,          // Add 결과. SaveChanges 시 INSERT 대상. Snapshot 없음.
	Modified,       // Update 명시 호출 또는 DetectChanges 결과. SaveChanges 시 UPDATE 대상.
	Deleted,        // Remove 결과. SaveChanges 시 DELETE 대상.
};

/**
 * DBSet<T> 가 트래킹하는 엔트리 1건의 단위 구조체.
 * 엔티티 shared_ptr + 상태 + 영속 영역 바이트 스냅샷.
 * 클래스 내부에 중첩하지 않고 외부에 두어 동일 엔티티 타입에 대한 외부 유틸리티(로그/디버깅/테스트) 가
 * DBSetEntry<T> 를 직접 참조할 수 있게 한다.
 */
template<typename T>
struct DBSetEntry
{
	std::shared_ptr<T> Entity;
	EEntityState State = EEntityState::Unchanged;
	// ColumnMeta.Offset/Size 영역만 채운 sizeof(T) 바이트 버퍼. 빈 vector = snapshot 없음(Add/Update 경로).
	std::vector<uint8> Snapshot;
};

/**
 * 타입 소거 베이스.
 * DBContext 의 Sets 맵이 unique_ptr<IDBSet> 으로 보관한다. SaveChanges 진입점이
 * 등록된 모든 DBSet 의 FlushChanges 를 한 트랜잭션 안에서 순차 호출한다.
 */
class IDBSet
{
public:
	virtual ~IDBSet() = default;

	/**
	 * SaveChanges 가 트랜잭션 BEGIN 직후 각 DBSet 에 대해 부른다.
	 * 구현부는 Added INSERT → Modified UPDATE → Deleted DELETE 순으로 발행하고 실패 시 false.
	 * 호출자(DBContext::SaveChanges)가 false 를 받으면 Rollback 후 DBContext 를 dead 로 전환한다.
	 */
	virtual bool FlushChanges(DBConnection& Conn) = 0;
};

/**
 * EF Core 의 DbSet<T> 등가. M6 부터 IdentityMap + ChangeTracker 역할을 함께 수행한다.
 *
 * [단일 버킷 구조]
 *  - Identified: unordered_map<PkType, DBSetEntry<T>>. M6 의 D1Server 는 모든 엔티티가 Add 시점에 PK 를 보유(앱 발급).
 *    Added 상태도 여기에 들어가므로 같은 Job 안에서 Add → Find(PK) 가 자연스럽게 hit 된다.
 *  - M8 IDENTITY 도입 시 PendingAdds 벡터를 분리해 PK 미발급 엔티티를 별도 수용 예정.
 *
 * [수명/소유권]
 *  - entity 는 shared_ptr<T> 소유. Find/Add 경로 모두 non-owning T* 반환 — DBContext 스코프(= Schedule 람다) 안에서 유효.
 *  - SaveChanges 호출 후 DBContext 는 dead 마킹됨 — 같은 DBContext 재사용 금지.
 *
 * [Snapshot 모델]
 *  - Find 는 DBOrm::Find 성공 시 ColumnMeta.Offset/Size 영역만 sizeof(T) 버퍼에 복사한다.
 *    런타임 필드(weak_ptr 등 non-trivially copyable)는 ColumnMeta 에 등록되지 않으므로 자연 제외 → memcpy 안전.
 *  - Add 는 신규 행이라 Snapshot 불필요(INSERT 가 전체 필드).
 *  - Update(shared_ptr) 는 Snapshot 을 비워둔다(= AsNoTracking 경로). SaveChanges 는 diff 건너뛰고 전체 UPDATE.
 *  - DetectChanges: Unchanged entry 의 현재 바이트와 Snapshot 을 ColumnMeta 영역별로 memcmp → diff 가 있으면 Modified 승격.
 *
 * [UPDATE 범위]
 *  - M6: 변경 감지 후 DBOrm::Update 재사용 — 전체 비-PK 컬럼 UPDATE. 간결/안전 우선.
 *  - M9+: diff 컬럼만 SET 하는 동적 UPDATE 로 확장 가능(prepared cache 키가 컬럼 비트마스크가 됨).
 *
 * [Thread safety]
 *  - DBJobQueue 가 단일 DB 워커 스레드를 보장 → 내부 자료구조 동기화 불필요.
 */
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

	/**
	 * M6 트래킹 Find.
	 *   hit (Unchanged/Added/Modified): [DBIdMap] HIT, 엔티티 ptr 반환. Added hit 은 같은 Job 내 Add 후 재조회 일관성을 보장.
	 *   hit (Deleted): nullptr — 논리적으로 삭제됐으므로 노출 금지.
	 *   miss: DBOrm::Find 로 1행 SELECT → Unchanged 로 등록 + Snapshot 캡처 → ptr 반환.
	 *   row 없음: nullptr + 맵에 저장 안 함(음성 캐싱 없음).
	 */
	T* Find(const PkType& PkValue)
	{
		// 1. identity Map에서 캐싱 확인
		if (auto It = Identified.find(PkValue); It != Identified.end())
		{
			if (It->second.State == EEntityState::Deleted)
			{
				std::cout << "[DBIdMap] HIT-DEL (Table=" << GetTableMetadata<T>().TableName << ", PK=" << PkValue << ") -> nullptr" << std::endl;
				return nullptr;
			}
			std::cout << "[DBIdMap] HIT  (Table=" << GetTableMetadata<T>().TableName << ", PK=" << PkValue << ")" << std::endl;
			return It->second.Entity.get();
		}

		std::cout << "[DBIdMap] MISS (Table=" << GetTableMetadata<T>().TableName << ", PK=" << PkValue << ")" << std::endl;
		
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

	/**
	 * 새 엔티티를 Added 로 등록. SaveChanges 시 INSERT 발행.
	 * 앱 발급 PK 전제 — Entity 의 PK 필드가 이미 채워져 있어야 한다(M6 범위).
	 * 같은 PK 로 이미 tracked 엔트리가 있으면 nullptr(중복 Add 방지).
	 */
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

	/**
	 * 외부에서 만든 엔티티를 Modified 로 바로 등록(AsNoTracking 업데이트 경로).
	 * 이미 Identified 에 같은 PK 가 있으면 false(Find/Update 경로를 섞지 않기 위함).
	 * SaveChanges 시 DBOrm::Update 가 전체 비-PK 컬럼 UPDATE.
	 */
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

	/**
	 * tracked 엔티티를 Deleted 로 전환. Find 또는 Add 반환 ptr 을 그대로 넘기는 게 정석.
	 * Added 상태를 Remove 하면 "Add 취소" 효과 — 맵에서 erase, SaveChanges 는 INSERT 도 DELETE 도 발행하지 않는다.
	 */
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

	/**
	 * M6 SaveChanges 내부 디스패치. 실행 순서:
	 *   1) DetectChanges — Unchanged 엔트리를 Snapshot 대비 비교 후 Modified 승격.
	 *   2) Added INSERT 일괄.
	 *   3) Modified UPDATE 일괄.
	 *   4) Deleted DELETE 일괄.
	 * 어느 단계든 DBOrm 이 false 면 즉시 중단 → 상위(DBContext::SaveChanges)에서 Rollback.
	 */
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
		static_assert(sizeof(T) == 0, "ExtractPk: unsupported PkType — add branch (uint64/string/wstring)");
		return PkType{}; // unreachable — static_assert 가 먼저 컴파일 에러를 낸다. MSVC 정적 분석기의 return-path 경고 회피용.
	}

	DBConnection& Conn;
	std::unordered_map<PkType, Entry> Identified;
};