#pragma once

#include "MemoryPool.h"
#include <memory>
#include <utility>

/** MemoryPool/PoolManager 기반 타입 안전 오브젝트 풀. */
template <typename T>
class ObjectPool
{
public:
	ObjectPool() = delete;

	/** PoolManager에서 메모리를 할당하고 T를 생성한다. */
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

	/** T의 소멸자를 호출하고 메모리를 PoolManager에 반환한다. */
	static void Push(T* Ptr) noexcept
	{
		if (Ptr == nullptr)
		{
			return;
		}

		Ptr->~T();
		DeallocateMemory(Ptr);
	}

	/** Pop으로 객체를 생성하고 커스텀 Deleter가 설정된 shared_ptr을 반환한다. */
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
