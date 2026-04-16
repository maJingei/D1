#pragma once

#include "Core/CoreMinimal.h"
#include "Job/LockQueue.h"
#include "Job/Job.h"

#include <atomic>
#include <memory>
#include <queue>

/**
 * Job 들을 직렬 실행하는 큐. LockQueue<JobRef> 를 소유한다.
 *
 * 동작 원리:
 *   - PushJob: Job 을 LockQueue 에 넣고 ReserveCount 를 1 증가시킨다.
 *     ReserveCount 가 0 → 1 전환이면 GlobalJobQueue 에 shared_from_this() 를 push 하여
 *     Flush Worker 가 이 큐를 소비할 수 있게 한다.
 *   - FlushJob: GlobalJobQueue 에서 꺼낸 Worker 가 호출한다.
 *     LockQueue 를 PopAll 하여 전부 실행한 뒤 ReserveCount 를 실행 수만큼 감소시킨다.
 *     남은 ReserveCount > 0 이면 Flush 도중 새 Job 이 Push 됐다는 뜻이므로
 *     GlobalJobQueue 에 재등록하여 누락 없이 처리한다.
 *
 * enable_shared_from_this<JobQueue> 단일 계보 — make_shared<JobQueue>() 로 생성하면
 * GlobalJobQueue 등록에 필요한 shared_ptr 를 자체 제공한다.
 * JobSerializer 를 대체하며, 파생 클래스 없이 composition 패턴으로 사용한다.
 */
class JobQueue : public std::enable_shared_from_this<JobQueue>
{
public:
	virtual ~JobQueue() = default;

	/** Job 을 큐에 추가한다. 필요 시 GlobalJobQueue 에 self 를 등록한다. */
	void PushJob(JobRef Job);

	/**
	 * 현재 큐에 쌓인 Job 을 전부 실행한다. Flush Worker 전용 호출.
	 * Flush 후 남은 예약이 있으면 GlobalJobQueue 에 재등록한다.
	 */
	void FlushJob();

private:
	LockQueue<JobRef> Queue;

	/**
	 * 예약 카운트: Push 시 +1, Flush 완료 시 실행한 만큼 -1.
	 * 0 → 1 전환 스레드가 GlobalJobQueue 등록 책임을 진다.
	 */
	std::atomic<int32> ReserveCount{0};
};