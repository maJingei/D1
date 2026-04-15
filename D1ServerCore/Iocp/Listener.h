#pragma once

#include <winsock2.h>
#include <array>
#include <memory>
#include "IocpObject.h"
#include "IocpEvent.h"
#include "NetAddress.h"

namespace D1
{
	class Service;

	/**
	 * AcceptEx 기반 연결 수용 클래스.
	 * IocpObject 를 상속하여 Accept 이벤트를 처리한다.
	 * Session 생성은 Service 의 SessionFactory 에 위임한다.
	 *
	 * 동시 in-flight AcceptEx 수는 kAcceptPoolSize. 단일 pending 구조는
	 * 대량 동접 부하 테스트에서 Accept 완료가 Recv/Send 이벤트에 밀려 starvation
	 * 되는 원인이었기 때문에 고정 크기 풀로 확장했다.
	 *
	 * 각 AcceptEvent 는 pre-create 된 Session 을 함께 들고 있다. 소켓 수명은 Session
	 * 소멸자가 책임지므로 Listener 는 직접 closesocket 을 호출하지 않는다.
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
		 * Listener 를 시작한다. 소켓 생성/옵션 설정/Bind/Listen/IOCP 등록/AcceptEx 풀 게시를 수행한다.
		 *
		 * @param Address    바인드할 주소 (IP + Port)
		 * @param InService  Session 생성 및 IOCP 등록에 사용할 Service (weak 참조)
		 * @return           시작 성공 여부
		 */
		bool Start(const NetAddress& Address, std::weak_ptr<Service> InService);

		/**
		 * Listen 소켓을 닫아 pending AcceptEx 들을 abort 완료로 drain 한다.
		 * 이후 ProcessAccept 는 abort 완료를 감지해 Session 을 정리하고 재게시를 생략한다.
		 * ServerService::Stop 에서만 호출된다.
		 */
		void Shutdown();

		Listener(const Listener&) = delete;
		Listener& operator=(const Listener&) = delete;
		Listener(Listener&&) = delete;
		Listener& operator=(Listener&&) = delete;

	private:
		/**
		 * Accept 완료 처리: 해당 슬롯의 pre-Session 을 IOCP 에 등록 + RegisterRecv 로 활성화 →
		 * 슬롯에 새로운 pre-Session 으로 AcceptEx 재게시. 셧다운 중이면 pre-Session 을
		 * Service 에서 제거하고 종료(재게시 안 함).
		 */
		void ProcessAccept(AcceptEvent& Event);

		/**
		 * 해당 슬롯에 AcceptEx 를 게시한다. Session 을 factory 로 생성 후 소켓 할당 → AcceptEx.
		 * 즉시 에러가 발생하면 세션을 정리하고 RetryBudget 범위에서 한 번만 재시도한다.
		 * ListenSocket 이 닫힌 상태(셧다운)면 게시를 건너뛴다.
		 */
		void PostAccept(AcceptEvent& Event, int32 RetryBudget = 1);

		/** 주어진 AcceptEvent 의 Session 을 Service 에서 제거 + 참조 해제(소켓은 Session 소멸자가 닫는다). */
		void DropPendingSession(AcceptEvent& Event);

		/** 동시 in-flight AcceptEx 수. 16 이면 대부분 부하에서 Accept 완료가 starvation 되지 않는다. */
		static constexpr int32 kAcceptPoolSize = 16;

		SOCKET ListenSocket = INVALID_SOCKET;
		std::array<AcceptEvent, kAcceptPoolSize> AcceptEvents;
		std::weak_ptr<Service> OwnerService;
	};
}