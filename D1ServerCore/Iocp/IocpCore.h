#pragma once

#include <winsock2.h>
#include "../Core/Types.h"

namespace D1
{
	class IocpObject;

	/**
	 * IOCP CompletionPort를 관리하는 Thin Wrapper.
	 * 워커 스레드는 관리하지 않으며, 외부에서 ThreadManager로 생성한 워커가
	 * Dispatch()를 루프에서 호출하는 구조이다.
	 */
	class IocpCore
	{
	public:
		IocpCore();
		~IocpCore();

		/** IOCP CompletionPort 핸들을 생성한다. */
		bool Initialize();

		/**
		 * IocpObject를 CompletionPort에 등록한다.
		 * CompletionKey로 IocpObject* 포인터를 전달한다.
		 *
		 * @param Object   등록할 IocpObject (GetHandle()로 핸들 획득)
		 * @return          등록 성공 여부
		 */
		bool Register(IocpObject* Object);

		/**
		 * GetQueuedCompletionStatus를 1회 호출하여 완료된 I/O를 처리한다.
		 * 워커 스레드에서 while(Core.Dispatch()) {} 형태로 사용한다.
		 *
		 * @param TimeoutMs   GQCS 대기 시간 (기본값: INFINITE)
		 * @return             true=계속 루프, false=종료 신호 감지
		 */
		bool Dispatch(uint32 TimeoutMs = INFINITE);

		/** IOCP 핸들을 반환한다. */
		HANDLE GetHandle() const { return IocpHandle; }

		IocpCore(const IocpCore&) = delete;
		IocpCore& operator=(const IocpCore&) = delete;
		IocpCore(IocpCore&&) = delete;
		IocpCore& operator=(IocpCore&&) = delete;

	private:
		HANDLE IocpHandle;
	};
}