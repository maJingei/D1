#include "DB/DBJobQueue.h"
#include "DB/DBConnection.h"
#include "DB/DBConnectionPool.h"
#include "Job/Job.h"

#include <iostream>

DBJobQueue& DBJobQueue::GetInstance()
{
	// GlobalJobQueue 와 동일: 함수 내부 static 포인터 + new. 프로세스 종료 시 leak 허용.
	static DBJobQueue* Instance = new DBJobQueue();
	return *Instance;
}

void DBJobQueue::PushJob(JobRef&& Job)
{
	// LockQueue 가 내부에서 mutex 로 push 를 보호하므로 별도 잠금 불필요.
	DBQueue.Push(std::move(Job));
}

void DBJobQueue::Schedule(DBJobFn Fn)
{
	// Pool 에서 빌리고 실행 후 반납하는 고정 패턴을 Job 본체로 감싼다.
	JobRef J = std::make_shared<Job>([Fn = std::move(Fn)]() mutable
	{
		DBConnection* Conn = DBConnectionPool::GetInstance().Pop();
		if (Conn == nullptr)
		{
			std::cout << "[DBJobQueue] skip: pool empty\n";
			return;
		}

		try
		{
			Fn(*Conn);
		}
		catch (const std::exception& Ex)
		{
			std::cout << "[DBJobQueue] job threw: " << Ex.what() << "\n";
		}
		catch (...)
		{
			std::cout << "[DBJobQueue] job threw: unknown\n";
		}

		DBConnectionPool::GetInstance().Push(Conn);
	});

	// virtual 디스패치로 우리 override 가 호출되어 DBQueue 에 적재된다.
	PushJob(std::move(J));
}

void DBJobQueue::Drain()
{
	// 도착한 Job 을 연속으로 모두 비운다. LockQueue::Pop 이 내부 mutex 로 보호됨.
	while (true)
	{
		JobRef Next;
		if (DBQueue.Pop(Next) == false)
			break;
		Next->Execute();
	}
}