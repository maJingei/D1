#pragma once

#include "MemoryPool.h"

namespace D1
{
	/**
	 * STL 호환 커스텀 할당자.
	 * PoolManager를 통해 풀 기반 메모리를 할당/해제한다.
	 *
	 * n=1: MemoryPool의 SLIST free-list 사용 (lock-free)
	 * n>1: 크기 클래스 버킷에서 할당
	 * MAX_POOL_SIZE 초과: 직접 VirtualAlloc/VirtualFree
	 */
	template <typename T>
	class StlAllocator
	{
	public:
		using value_type = T;

		StlAllocator() noexcept = default;

		template <typename U>
		StlAllocator(const StlAllocator<U>&) noexcept {}

		T* allocate(SizeType Count)
		{
			if (Count == 0)
			{
				return nullptr;
			}

			SizeType TotalSize = sizeof(T) * Count;

			// MAX_POOL_SIZE 초과 시 직접 VirtualAlloc
			if (TotalSize > MAX_POOL_SIZE)
			{
				return static_cast<T*>(LargeAllocate(TotalSize));
			}

			MemoryPool* Pool = PoolManager::GetInstance().GetPool(TotalSize);
			return static_cast<T*>(Pool->Allocate());
		}

		void deallocate(T* Ptr, SizeType Count) noexcept
		{
			if (Ptr == nullptr)
			{
				return;
			}

			SizeType TotalSize = sizeof(T) * Count;

			if (TotalSize > MAX_POOL_SIZE)
			{
				LargeDeallocate(Ptr);
				return;
			}

			MemoryPool* Pool = PoolManager::GetInstance().GetPool(TotalSize);
			Pool->Deallocate(Ptr);
		}
	};

	// Stateless allocator — 모든 인스턴스 동등
	template <typename T, typename U>
	bool operator==(const StlAllocator<T>&, const StlAllocator<U>&) noexcept { return true; }

	template <typename T, typename U>
	bool operator!=(const StlAllocator<T>&, const StlAllocator<U>&) noexcept { return false; }
}