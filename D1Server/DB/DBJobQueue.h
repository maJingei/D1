#pragma once

#include "Core/CoreMinimal.h"
#include "Job/JobQueue.h"
#include "Job/LockQueue.h"

#include <functional>

class DBConnection;

/** DB 전용 Job 시그니처. DB 워커가 Pool 에서 빌린 연결을 인자로 전달한다. */
using DBJobFn = std::function<void(DBConnection&)>;

/**
 * DB 작업 전용 JobQueue. JobQueue 를 상속해 Job/JobRef 포맷을 재사용하되,
 * PushJob 을 override 해 IOCP 워커가 inline FlushJob 으로 DB 호출에 블록되는 것을 막는다.
 */
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

	/**
	 * Pool.Pop → Fn(conn) → Pool.Push 를 감싼 Job 을 만들어 PushJob 한다. IOCP 워커가 한 줄로 사용할 수 있는 고수준 헬퍼.
	 */
	void Schedule(DBJobFn Fn);

	/** DB 스레드 루프용: 큐에 쌓인 Job 을 전부 꺼내 순서대로 실행한 뒤 반환. 비어있으면 즉시 반환. */
	void Drain();

private:
	// JobQueue base 의 Queue 는 inline-flush 경로 전용이므로 건드리지 않는다.
	LockQueue<JobRef> DBQueue;
};