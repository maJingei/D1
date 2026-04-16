#pragma once

#include "Core/CoreMinimal.h"

#include <mutex>
#include <queue>

class JobQueue;

/**
 * Flush 대기 중인 JobQueue 의 전역 큐.
 *
 * Flush Worker 스레드들이 여기서 Pop 하여 FlushJob() 을 호출한다.
 * 큐가 비어있을 때는 Hybrid Wait (짧은 스핀 후 WaitOnAddress sleep) 로
 * CPU 를 낭비하지 않는다.
 *
 * Push 시 WakeByAddressSingle 로 대기 중인 Worker 하나를 깨운다.
 */
class GlobalJobQueue
{
public:
	/** Leaked singleton: GetInstance() 최초 호출 시 생성, 프로세스 종료 시 회수. */
	static GlobalJobQueue& GetInstance();

	/** JobQueue 를 큐 끝에 추가하고 대기 Worker 하나를 깨운다. */
	void Push(std::shared_ptr<JobQueue> InJobQueue);

	/**
	 * 큐에서 JobQueue 하나를 꺼낸다.
	 * 큐가 비어있으면 Hybrid Wait (스핀 → WaitOnAddress) 로 대기한다.
	 *
	 * @param bShouldRun  false 가 되면 대기를 중단하고 nullptr 을 반환한다 (종료 신호).
	 */
	std::shared_ptr<JobQueue> Pop(const std::atomic<bool>& bShouldRun);

	/** 대기 중인 모든 Worker 를 깨운다. 종료 시퀀스에서 호출. */
	void WakeAll();

private:
	GlobalJobQueue() = default;

	mutable std::mutex Mutex;
	std::queue<std::shared_ptr<JobQueue>> JobQueues;

	/** 큐 상태 변화를 WaitOnAddress 로 감지하기 위한 카운터. */
	uint32 WakeCounter = 0;
};
