#include "Job/JobQueue.h"
#include "Job/GlobalJobQueue.h"

//현재 스레드가 이미 JobQueue Flush 루프 안에 있는지 표시한다.
thread_local bool tFlushing = false;

void JobQueue::PushJob(JobRef&& Job)
{
	// 1. ReserveCount 를 먼저 증가시킨다.
	const int32 PrevCount = ReserveCount.fetch_add(1);

	// 2. Job 을 LockQueue 에 push 한다.
	Queue.Push(std::move(Job));

	// 3. 이전에 큐가 비어있지 않았다면 이미 다른 주체가 Flush 를 예약했으므로 반환.
	if (PrevCount != 0)
		return;

	// 4. 현재 스레드가 이미 다른 Flush 루프 안에 있다면 체인 실행을 피하려 GlobalJobQueue 에 위임.
	if (tFlushing)
	{
		GlobalJobQueue::GetInstance().Push(shared_from_this());
		return;
	}

	// 5. 첫 진입 + 재진입 아님 → 호출자 스레드가 그대로 Flush 를 돌려 캐시 온기/디스패치 왕복을 절약.
	FlushJob();
}

void JobQueue::FlushJob()
{
	tFlushing = true;

    // Count와 시간으로 스케줄링. 일정 Count가 지나면 JobQueue 스케줄링을 놔버림. 
	const uint64 StartMs = ::GetTickCount64();
	int32 ExecutedCount = 0;

	while (true)
	{
		// 2. 큐에서 Job 한 개를 논블로킹으로 꺼낸다. 비어 있으면 루프 종료.
		JobRef Next;
		if (Queue.Pop(Next) == false)
			break;

		// 3. 락 밖에서 Job 실행.
		Next->Execute();
		++ExecutedCount;

		// 4. Budget 상한 체크 — Count 먼저, 그 다음 Time.
		if (ExecutedCount >= JobBudgetCount)
			break;
		if (::GetTickCount64() - StartMs >= JobBudgetMs)
			break;
	}

	// 5. 실제 실행한 만큼 ReserveCount 를 차감한다.
	const int32 Remaining = ReserveCount.fetch_sub(ExecutedCount) - ExecutedCount;

	// 6. 가드 해제 후 남은 일감은 GlobalJobQueue 로 넘겨 FIFO 순서에 다시 편입.
	tFlushing = false;
	if (Remaining > 0)
		GlobalJobQueue::GetInstance().Push(shared_from_this());
}