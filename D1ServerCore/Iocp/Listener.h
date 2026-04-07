#pragma once

#include <winsock2.h>
#include "IocpObject.h"
#include "IocpEvent.h"
#include "../Core/ContainerTypes.h"

namespace D1
{
	class IocpCore;
	class Session;

	/**
	 * AcceptEx 기반 연결 수용 클래스.
	 * IocpObject를 상속하여 Accept 이벤트를 처리하고 Session을 생성/관리한다.
	 */
	class Listener : public IocpObject
	{
	public:
		Listener();
		~Listener();

		// IocpObject 인터페이스
		HANDLE GetHandle() override;
		void Dispatch(IocpEvent* Event, int32 NumOfBytes) override;

		/**
		 * Listener를 시작한다.
		 * 소켓 생성/옵션 설정/Bind/Listen/IOCP 등록/AcceptEx 게시를 수행한다.
		 *
		 * @param Address   바인드할 주소 (IP + Port)
		 * @param Core      Session 등록에 사용할 IocpCore
		 * @return           시작 성공 여부
		 */
		bool Start(const SOCKADDR_IN& Address, IocpCore* Core);

		/** Accept 완료 처리: Session 생성 → IOCP 등록 → Recv 시작 → 새 AcceptEx 게시 */
		void ProcessAccept();

		/** AcceptEx를 게시한다. (WSASocket으로 클라이언트 소켓 미리 생성) */
		void PostAccept();

		/** 모든 세션을 정리하고 Listen 소켓을 닫는다. */
		void CloseAllSessions();

		Listener(const Listener&) = delete;
		Listener& operator=(const Listener&) = delete;
		Listener(Listener&&) = delete;
		Listener& operator=(Listener&&) = delete;

	private:
		SOCKET ListenSocket = INVALID_SOCKET;
		AcceptEvent AcceptIocpEvent;
		char AcceptBuffer[64] = {};           // AcceptEx 주소 버퍼 (비동기 완료까지 유지 필수)
		Set<Session*> Sessions;               // 활성 세션 임시 관리 (TODO: 추후 ServerService로 이전)
		IocpCore* CoreRef = nullptr;
	};
}