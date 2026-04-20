#include "Job/GlobalJobQueue.h"

#include <iostream>

#include "Job/JobQueue.h"
#include "Core/CoreMinimal.h"

#include <windows.h>

GlobalJobQueue& GlobalJobQueue::GetInstance()
{
	// 포인터 기반 지연 생성 — 정적 소멸 순서 문제 회피.
	static GlobalJobQueue* Instance = new GlobalJobQueue();
	return *Instance;
}

void GlobalJobQueue::Push(std::shared_ptr<JobQueue> InJobQueue)
{
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		JobQueues.push(std::move(InJobQueue));
		// WakeCounter 를 증가시켜 WaitOnAddress 로 잠든 Worker 를 깨운다.
		WakeCounter++;
	}
	// 락 밖에서 Wake — WakeByAddressSingle 로 대기 Worker 하나만 깨운다.
	::WakeByAddressSingle(&WakeCounter);
}

std::shared_ptr<JobQueue> GlobalJobQueue::Pop(const std::atomic<bool>& bShouldRun)
{
	while (bShouldRun.load(std::memory_order_relaxed))
	{
		// 1단계: 짧은 스핀으로 큐를 확인한다 (SPIN_COUNT 회).
		for (uint32 SpinIdx = 0; SpinIdx < SPIN_COUNT; SpinIdx++)
		{
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (JobQueues.empty() == false)
				{
					std::shared_ptr<JobQueue> Result = JobQueues.front();
					JobQueues.pop();
					// std::cout << "[FlushWorker] TID=" << std::this_thread::get_id()	<< " PopQueue=" << Result.get() << "\n";
					return Result;
				}
			}
			// 스핀 중 CPU 힌트 — 다른 하이퍼스레드에게 양보한다. -> 그니까 if문을 실행할 때 cpu가 추측 실행으로 파이프라인에 추측 실행 결과를 가져와서 실행하는 것처럼, Spin도 마찬가지. 근데 YieldProcessor(mm_pause)가 억제하는 것이 바로 추측을 자제하라는 뜻.
			::YieldProcessor();
		}

		// 2단계: 스핀에서 못 찾으면 WaitOnAddress 로 슬립한다.
		uint32 CapturedCounter;
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			// 슬립 직전 한 번 더 확인 — Wake 와 슬립 사이 경쟁(ABA) 방지.
			if (JobQueues.empty() == false)
			{
				std::shared_ptr<JobQueue> Result = JobQueues.front();
				JobQueues.pop();
				// std::cout << "[FlushWorker] TID=" << std::this_thread::get_id()	<< " PopQueue=" << Result.get() << " (post-sleep)\n";
				return Result;
			}
			CapturedCounter = WakeCounter;
		}
		// WakeCounter 가 CapturedCounter 와 다르면 즉시 리턴(이미 깨어있음).
		::WaitOnAddress(&WakeCounter, &CapturedCounter, sizeof(uint32), INFINITE);
	}

	// bShouldRun == false — 종료 신호
	return nullptr;
}

void GlobalJobQueue::WakeAll()
{
	// 모든 대기 Worker 를 깨워 종료 루프를 돌게 한다.
	::WakeByAddressAll(&WakeCounter);
}