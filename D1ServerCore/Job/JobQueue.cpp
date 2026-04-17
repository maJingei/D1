#include "Job/JobQueue.h"
#include "Job/GlobalJobQueue.h"

void JobQueue::PushJob(JobRef Job)
{
	// 1. ReserveCount 를 먼저 증가시킨다.
	//    fetch_add 이전 값이 0 이면 이 스레드가 0→1 전환 주체 = GlobalJobQueue 등록 담당.
	const int32 PrevCount = ReserveCount.fetch_add(1);

	// 2. Job 을 LockQueue 에 push 한다.
	Queue.Push(std::move(Job));

	// 3. 이전에 큐가 비어있었다면 (PrevCount == 0) GlobalJobQueue 에 self 를 등록한다.
	if (PrevCount == 0)
		GlobalJobQueue::GetInstance().Push(shared_from_this());
}

void JobQueue::FlushJob()
{
	// 1. Flush 시작 전의 ReserveCount 를 기억해 둔다.
	const int32 CountToFlush = ReserveCount.load();

	// 2. PopAll 로 스냅샷을 꺼낸 뒤 락 밖에서 실행한다.
	std::queue<JobRef> Snapshot;
	Queue.PopAll(Snapshot);
	while (Snapshot.empty() == false)
	{
		Snapshot.front()->Execute();
		Snapshot.pop();
	}

	// 3. 처리한 만큼 ReserveCount 를 감소시킨다.
	//    차감 후 남은 값 > 0 이면 Flush 도중 새 Job 이 Push 됐다는 뜻이다.
	const int32 Remaining = ReserveCount.fetch_sub(CountToFlush) - CountToFlush;
	if (Remaining > 0)
		GlobalJobQueue::GetInstance().Push(shared_from_this());
}