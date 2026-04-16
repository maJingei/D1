#pragma once

#include "../Core/CoreMinimal.h"
#include "JobQueue.h"

#include <atomic>
#include <memory>

/**
 * JobQueue 를 소유하고 단일 스레드 직렬 실행을 보장하는 베이스 클래스.
 *
 * 동작 원리:
 *   - PushJob: Job 을 내부 JobQueue 에 넣는다. 동시에 ReserveCount 를 1 증가시킨다.
 *     ReserveCount 가 1 (0 → 1 전환) 인 경우, 즉 이 Serializer 가 아직 GlobalJobQueue
 *     에 등록되지 않은 상태라면 GlobalJobQueue 에 self(shared_ptr) 를 push 한다.
 *     이미 GlobalJobQueue 에 올라가 있거나 다른 스레드가 Flush 중이면 단순 Job 추가로 끝난다.
 *   - FlushJob: GlobalJobQueue 에서 꺼낸 Flush Worker 가 호출한다.
 *     내부 JobQueue 의 Job 을 전부 실행(Flush) 한 뒤 ReserveCount 를 원래 Flush 전
 *     큐 크기만큼 감소시킨다. 남은 ReserveCount > 0 이면 Flush 도중 새 Job 이 Push 됐다는
 *     의미이므로 GlobalJobQueue 에 다시 self 를 등록하여 누락 없이 처리한다.
 *
 * enable_shared_from_this 를 이 클래스에서 제거한 이유:
 *   Session 처럼 다른 경로(IocpObject)로 이미 enable_shared_from_this 를 보유한 파생이
 *   JobSerializer 도 상속할 때 다중 enable_shared_from_this 계보 충돌이 발생한다.
 *   이를 방지하기 위해 GlobalJobQueue 에 등록할 shared_ptr<JobSerializer> 는
 *   GetSerializerRef() 가상 함수로 파생 클래스에서 직접 공급받는다.
 */
class JobSerializer
{
public:
	virtual ~JobSerializer() = default;

	/** Job 을 큐에 추가한다. 필요 시 GlobalJobQueue 에 self 를 등록한다. */
	void PushJob(JobRef Job);

	/**
	 * 현재 큐에 쌓인 Job 을 전부 실행한다. Flush Worker 전용 호출.
	 * Flush 후 남은 예약이 있으면 GlobalJobQueue 에 재등록한다.
	 */
	void FlushJob();

protected:
	/**
	 * 파생 클래스가 자신의 enable_shared_from_this 계보로 얻은 shared_ptr 를
	 * static_pointer_cast<JobSerializer> 로 반환한다.
	 *
	 * - GameRoom  : enable_shared_from_this<GameRoom>::shared_from_this() 캐스트
	 * - Session   : IocpObject(enable_shared_from_this<IocpObject>)::shared_from_this() 캐스트
	 *
	 * GlobalJobQueue 등록 시 사용하므로 반드시 유효한 shared_ptr 를 반환해야 한다.
	 */
	virtual JobSerializerRef GetSerializerRef() = 0;

private:
	JobQueue Queue;

	/**
	 * 예약 카운트: Push 시 +1, Flush 완료 시 실행한 만큼 -1.
	 * 0 → 1 전환 스레드가 GlobalJobQueue 등록 책임을 진다.
	 * 이 방식으로 동시에 여러 Worker 가 같은 Serializer 를 Flush 하는 일을 막는다.
	 */
	std::atomic<int32> ReserveCount{ 0 };
};
