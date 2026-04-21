#pragma once

#include "Core/CoreMinimal.h"
#include "Job/LockQueue.h"
#include "Job/Job.h"

#include <atomic>
#include <memory>
#include <queue>


/** 한 FlushJob 호출에서 실행할 수 있는 최대 Job 개수. */
static constexpr int32 JobBudgetCount = 64;

/** 한 FlushJob 호출에서 허용되는 최대 누적 실행 시간 (ms). */
static constexpr uint64 JobBudgetMs = 5;

/** Job 들을 직렬 실행하는 큐. */
class JobQueue : public std::enable_shared_from_this<JobQueue>
{
public:
	virtual ~JobQueue() = default;

	/**
	 * 멤버 함수와 인자들을 Job 으로 감싸 자기 자신의 큐에 push 한다.
	 * T 는 MemFunc 의 소유 클래스로 자동 추론되며 JobQueue 의 파생 클래스여야 한다.
	 */
	template<typename T, typename Ret, typename... Args, typename... ActualArgs>
	void DoAsync(Ret(T::*MemFunc)(Args...), ActualArgs&&... InArgs)
	{
		std::shared_ptr<T> Self = std::static_pointer_cast<T>(shared_from_this());
		PushJob(std::make_shared<Job>(Self, MemFunc, std::forward<ActualArgs>(InArgs)...));
	}

	/**
	 * Job 을 큐에 추가한다. 큐가 비어있던 첫 진입이고 현재 스레드가 다른 Flush 루프 안에 있지 않다면
	 * inline FlushJob 을 실행하며, 그렇지 않으면 GlobalJobQueue 로 위임한다.
	 * DB 전용 파생(DBJobQueue) 은 inline flush 를 피하기 위해 이 메서드를 override 한다.
	 */
	virtual void PushJob(JobRef&& Job);

	/**
	 * 현재 큐에 쌓인 Job 을 전부 실행한다.
	 */
	void FlushJob();

private:
	LockQueue<JobRef> Queue;

	/** 예약 카운트: Push 시 +1, Flush 완료 시 실행한 만큼 -1. */
	std::atomic<int32> ReserveCount{0};
};