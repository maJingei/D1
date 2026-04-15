#pragma once

#include "../Core/Types.h"
#include "JobQueue.h"

#include <atomic>
#include <memory>

namespace D1
{
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
	 * GameRoom 처럼 파생 클래스가 shared_ptr 로 관리되어야 GlobalJobQueue push 가 가능하므로
	 * std::enable_shared_from_this 를 상속한다.
	 */
	class JobSerializer : public std::enable_shared_from_this<JobSerializer>
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

	private:
		JobQueue Queue;

		/**
		 * 예약 카운트: Push 시 +1, Flush 완료 시 실행한 만큼 -1.
		 * 0 → 1 전환 스레드가 GlobalJobQueue 등록 책임을 진다.
		 * 이 방식으로 동시에 여러 Worker 가 같은 Serializer 를 Flush 하는 일을 막는다.
		 */
		std::atomic<int32> ReserveCount{ 0 };
	};
}