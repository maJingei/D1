#pragma once

#include "MemoryPool.h"
#include <memory>
#include <utility>

namespace D1
{
	/**
	 * MemoryPool/PoolManager 기반 타입 안전 오브젝트 풀.
	 * 정적 메서드만 제공하는 stateless 유틸리티 클래스.
	 * StlAllocator와 동일한 PoolManager 경유 패턴을 따른다.
	 *
	 * - Pop: 메모리 할당 + placement new로 객체 생성
	 * - Push: 명시적 소멸자 호출 + 메모리 반환
	 * - MakeShared: Pop + std::shared_ptr with 커스텀 Deleter (자동 반환)
	 *
	 * sizeof(T) <= MAX_POOL_SIZE: PoolManager 경유 (lock-free SLIST)
	 * sizeof(T) > MAX_POOL_SIZE: LargeAllocate/LargeDeallocate 폴백 (VirtualAlloc)
	 *
	 * @tparam T  관리할 객체 타입
	 */
	template <typename T>
	class ObjectPool
	{
	public:
		ObjectPool() = delete;

		/**
		 * PoolManager에서 메모리를 할당하고 T를 생성한다.
		 * 생성자 예외 발생 시 메모리를 안전하게 반환한다.
		 *
		 * @param InArgs  T 생성자에 전달할 인자들
		 * @return         생성된 T 객체 포인터
		 */
		template <typename... Args>
		static T* Pop(Args&&... InArgs)
		{
			void* Memory = AllocateMemory();

			// 예외 안전: 생성자 throw 시 메모리 누수 방지
			try
			{
				return new (Memory) T(std::forward<Args>(InArgs)...);
			}
			catch (...)
			{
				DeallocateMemory(Memory);
				throw;
			}
		}

		/**
		 * T의 소멸자를 호출하고 메모리를 PoolManager에 반환한다.
		 *
		 * @param Ptr  반환할 객체 포인터 (nullptr 시 no-op)
		 */
		static void Push(T* Ptr) noexcept
		{
			if (Ptr == nullptr)
			{
				return;
			}

			Ptr->~T();
			DeallocateMemory(Ptr);
		}

		/**
		 * Pop으로 객체를 생성하고 커스텀 Deleter가 설정된 shared_ptr을 반환한다.
		 * shared_ptr 참조 카운트가 0이 되면 자동으로 Push하여 풀에 반환.
		 *
		 * 주의: shared_ptr의 control block은 ::operator new로 별도 힙 할당된다.
		 * 이는 std::shared_ptr(ptr, deleter) 패턴의 고유 비용이며,
		 * 객체 메모리만 PoolManager에서 할당된다.
		 *
		 * @param InArgs  T 생성자에 전달할 인자들
		 * @return         커스텀 Deleter가 설정된 std::shared_ptr<T>
		 */
		template <typename... Args>
		static std::shared_ptr<T> MakeShared(Args&&... InArgs)
		{
			T* Ptr = Pop(std::forward<Args>(InArgs)...);
			return std::shared_ptr<T>(Ptr, [](T* P) { ObjectPool<T>::Push(P); });
		}

	private:
		/** sizeof(T)에 따라 PoolManager 또는 LargeAllocate로 메모리를 할당한다. */
		static void* AllocateMemory()
		{
			if constexpr (sizeof(T) > MAX_POOL_SIZE)
			{
				return LargeAllocate(sizeof(T));
			}
			else
			{
				return PoolManager::GetInstance().GetPool(sizeof(T))->Allocate();
			}
		}

		/** sizeof(T)에 따라 PoolManager 또는 LargeDeallocate로 메모리를 반환한다. */
		static void DeallocateMemory(void* Ptr) noexcept
		{
			if constexpr (sizeof(T) > MAX_POOL_SIZE)
			{
				LargeDeallocate(Ptr);
			}
			else
			{
				PoolManager::GetInstance().GetPool(sizeof(T))->Deallocate(Ptr);
			}
		}
	};
}