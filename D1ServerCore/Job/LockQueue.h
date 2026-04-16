#pragma once

#include "Core/CoreMinimal.h"

#include <mutex>
#include <queue>

/**
 * std::queue<T> 의 push / pop / Clear / PopAll 을 mutex 로 래핑한 스레드 안전 큐.
 *
 * JobQueue 와 GlobalJobQueue 의 내부 저장소로 사용된다.
 */
template<typename T>
class LockQueue
{
public:
	/** 좌값 아이템을 복사하여 큐 끝에 추가한다. */
	void Push(const T& Item)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Queue.push(Item);
	}

	/** 우값 아이템을 이동하여 큐 끝에 추가한다. */
	void Push(T&& Item)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Queue.push(std::move(Item));
	}

	/**
	 * 큐 맨 앞의 아이템을 꺼낸다.
	 *
	 * @param OutItem  꺼낸 아이템을 받을 참조
	 * @return         큐가 비어있으면 false
	 */
	bool Pop(T& OutItem)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (Queue.empty())
			return false;
		OutItem = std::move(Queue.front());
		Queue.pop();
		return true;
	}

	/** 큐를 비운다. */
	void Clear()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		std::queue<T> Empty;
		Queue.swap(Empty);
	}

	/**
	 * 큐 전체를 OutQueue 로 스왑한다.
	 * 락 밖에서 일괄 처리(Flush)할 때 사용한다.
	 */
	void PopAll(std::queue<T>& OutQueue)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Queue.swap(OutQueue);
	}

	/** 현재 큐가 비어있으면 true. */
	bool IsEmpty() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Queue.empty();
	}

private:
	mutable std::mutex Mutex;
	std::queue<T> Queue;
};