#pragma once

#include <winsock2.h>
#include <memory>
#include "IocpObject.h"
#include "IocpEvent.h"

namespace D1
{
	class Service;

	/**
	 * AcceptEx 기반 연결 수용 클래스.
	 * IocpObject를 상속하여 Accept 이벤트를 처리한다.
	 * Session 생성은 Service의 SessionFactory에 위임한다.
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
		 * @param Address    바인드할 주소 (IP + Port)
		 * @param InService  Session 생성 및 IOCP 등록에 사용할 Service (weak 참조)
		 * @return           시작 성공 여부
		 */
		bool Start(const SOCKADDR_IN& Address, std::weak_ptr<Service> InService);

		/** Accept 완료 처리: Session 생성 → IOCP 등록 → Recv 시작 → 새 AcceptEx 게시 */
		void ProcessAccept();

		/** AcceptEx를 게시한다. (WSASocket으로 클라이언트 소켓 미리 생성) */
		void PostAccept();

		Listener(const Listener&) = delete;
		Listener& operator=(const Listener&) = delete;
		Listener(Listener&&) = delete;
		Listener& operator=(Listener&&) = delete;

	private:
		SOCKET ListenSocket = INVALID_SOCKET;
		AcceptEvent AcceptIocpEvent;
		char AcceptBuffer[64] = {};           // AcceptEx 주소 버퍼 (비동기 완료까지 유지 필수)
		std::weak_ptr<Service> ServiceRef;
	};
}