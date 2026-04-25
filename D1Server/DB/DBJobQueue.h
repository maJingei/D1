#pragma once

#include "Core/CoreMinimal.h"
#include "Job/JobQueue.h"
#include "Job/LockQueue.h"

#include <functional>

class DBConnection;

/** DB 전용 Job 시그니처. DB 워커가 Pool 에서 빌린 연결을 인자로 전달한다. */
using DBJobFn = std::function<void(DBConnection&)>;

/** DB 전용 JobQueue — PushJob override 로 IOCP 워커의 inline FlushJob 우회, DB 워커 스레드가 Drain 담당. */
class DBJobQueue : public JobQueue
{
public:
	/** Leaked singleton: GetInstance 내부 static 포인터(GlobalJobQueue 와 동일). 프로세스 종료 시 leak 허용. */
	static DBJobQueue& GetInstance();

	DBJobQueue() = default;
	~DBJobQueue() override = default;

	DBJobQueue(const DBJobQueue&) = delete;
	DBJobQueue& operator=(const DBJobQueue&) = delete;
	DBJobQueue(DBJobQueue&&) = delete;
	DBJobQueue& operator=(DBJobQueue&&) = delete;

	/** Job 을 자체 DBQueue 에 밀어넣는다. 기반의 inline-flush 경로는 타지 않는다. */
	void PushJob(JobRef&& Job) override;

	/** Pool.Pop → Fn(conn) → Pool.Push 감싸서 PushJob. Fn 은 rvalue 만 수용(일회성 소비). */
	void Schedule(DBJobFn&& Fn);

	/** DB 스레드 루프용: 큐에 쌓인 Job 을 전부 꺼내 순서대로 실행한 뒤 반환. 비어있으면 즉시 반환. */
	void Drain();

private:
	// JobQueue base 의 Queue 는 inline-flush 경로 전용이므로 건드리지 않는다.
	LockQueue<JobRef> DBQueue;
};