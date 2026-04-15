#include "JobSerializer.h"
#include "GlobalJobQueue.h"

namespace D1
{
	void JobSerializer::PushJob(JobRef Job)
	{
		// 1. Job 을 큐에 넣기 전에 ReserveCount 를 증가시킨다.
		//    fetch_add 이전 값이 0 이면, 이 스레드가 0→1 전환 주체 = GlobalJobQueue 등록 담당.
		const int32 PrevCount = ReserveCount.fetch_add(1);

		// 2. Job 을 내부 큐에 push 한다.
		Queue.Push(std::move(Job));

		// 3. 이전에 큐가 비어있었다면 (PrevCount == 0) GlobalJobQueue 에 self 를 등록한다.
		//    이미 GlobalJobQueue 에 올라가 있거나 Flush Worker 가 처리 중이면 skip.
		if (PrevCount == 0)
			GlobalJobQueue::GetInstance().Push(shared_from_this());
	}

	void JobSerializer::FlushJob()
	{
		// 1. Flush 시작 전의 ReserveCount 를 기억해 둔다.
		//    이 값만큼만 처리하고, Flush 도중 추가된 Job 은 다음 회차에 맡긴다.
		const int32 CountToFlush = ReserveCount.load();

		// 2. 내부 큐를 전부 실행한다.
		Queue.Flush();

		// 3. 처리한 만큼 ReserveCount 를 감소시킨다.
		//    차감 후 남은 값 > 0 이면 Flush 도중 새 Job 이 Push 됐다는 뜻이다.
		const int32 Remaining = ReserveCount.fetch_sub(CountToFlush) - CountToFlush;
		if (Remaining > 0)
		{
			// Flush 도중 쌓인 Job 이 있으므로 GlobalJobQueue 에 재등록해 누락을 방지한다.
			GlobalJobQueue::GetInstance().Push(shared_from_this());
		}
	}
}