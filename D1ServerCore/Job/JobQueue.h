#pragma once

#include "../Core/Types.h"
#include "Job.h"

#include <mutex>
#include <queue>

namespace D1
{
	/**
	 * Job 들을 순서대로 보관하는 스레드 안전 큐.
	 *
	 * std::mutex 로 내부 Queue 접근을 직렬화한다.
	 * Flush() 는 현재 쌓인 Job 을 전부 스왑하여 락 바깥에서 실행하므로
	 * Execute 중 새 Push 가 들어와도 교착이 발생하지 않는다.
	 */
	class JobQueue
	{
	public:
		/** Job 을 큐 끝에 추가한다. */
		void Push(JobRef Job)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Jobs.push(std::move(Job));
		}

		/**
		 * 큐 맨 앞의 Job 하나를 꺼낸다. 비어있으면 nullptr 반환.
		 * JobSerializer 내부의 FlushJob 루프에서 사용한다.
		 */
		JobRef Pop()
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (Jobs.empty())
				return nullptr;
			JobRef Front = Jobs.front();
			Jobs.pop();
			return Front;
		}

		/**
		 * 현재 큐에 쌓인 Job 을 모두 실행하고 비운다.
		 *
		 * 스왑 패턴: 락 안에서 내부 큐를 로컬로 교체한 뒤 락 밖에서 Execute 를 호출한다.
		 * Execute 중 새로운 Push 가 들어와도 Mutex 경쟁만 발생할 뿐 교착은 생기지 않는다.
		 */
		void Flush()
		{
			std::queue<JobRef> Snapshot;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				Snapshot.swap(Jobs);
			}
			while (Snapshot.empty() == false)
			{
				JobRef J = Snapshot.front();
				Snapshot.pop();
				J->Execute();
			}
		}

		/** 현재 큐에 Job 이 있으면 true. */
		bool IsEmpty() const
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			return Jobs.empty();
		}

	private:
		mutable std::mutex Mutex;
		std::queue<JobRef> Jobs;
	};
}