#pragma once

#include "Core/CoreMinimal.h"
#include "Job/LockQueue.h"
#include "Job/Job.h"

#include <atomic>
#include <memory>
#include <queue>

/** Job 들을 직렬 실행하는 큐. */
class JobQueue : public std::enable_shared_from_this<JobQueue>
{
public:
	virtual ~JobQueue() = default;

	/** Job 을 큐에 추가한다. 필요 시 GlobalJobQueue 에 self 를 등록한다. */
	void PushJob(JobRef Job);

	/** 현재 큐에 쌓인 Job 을 전부 실행한다. */
	void FlushJob();

private:
	LockQueue<JobRef> Queue;

	/** 예약 카운트: Push 시 +1, Flush 완료 시 실행한 만큼 -1. */
	std::atomic<int32> ReserveCount{0};
};