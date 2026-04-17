#pragma once

#include <winsock2.h>
#include <array>
#include <memory>
#include "IocpObject.h"
#include "IocpEvent.h"
#include "NetAddress.h"

class Service;

/** AcceptEx 기반 연결 수용 클래스. */
class Listener : public IocpObject
{
public:
	Listener();
	~Listener();

	// IocpObject 인터페이스
	HANDLE GetHandle() override;
	void Dispatch(IocpEvent* Event, int32 NumOfBytes) override;

	/** Listener 를 시작한다. */
	bool Start(const NetAddress& Address, std::weak_ptr<Service> InService);

	/** Listen 소켓을 닫아 pending AcceptEx 들을 abort 완료로 drain 한다. */
	void Shutdown();

	Listener(const Listener&) = delete;
	Listener& operator=(const Listener&) = delete;
	Listener(Listener&&) = delete;
	Listener& operator=(Listener&&) = delete;

private:
	/** Accept 완료 처리: 해당 슬롯의 pre-Session 을 IOCP 에 등록 + RegisterRecv 로 활성화 → 슬롯에 새로운 pre-Session 으로 AcceptEx 재게시. */
	void ProcessAccept(AcceptEvent& Event);

	/** 해당 슬롯에 AcceptEx 를 게시한다. */
	void PostAccept(AcceptEvent& Event, int32 RetryBudget = 1);

	/** 주어진 AcceptEvent 의 Session 을 Service 에서 제거 + 참조 해제(소켓은 Session 소멸자가 닫는다). */
	void DropPendingSession(AcceptEvent& Event);

	/** 동시 in-flight AcceptEx 수. 16 이면 대부분 부하에서 Accept 완료가 starvation 되지 않는다. */
	static constexpr int32 kAcceptPoolSize = 16;

	SOCKET ListenSocket = INVALID_SOCKET;
	std::array<AcceptEvent, kAcceptPoolSize> AcceptEvents;
	std::weak_ptr<Service> OwnerService;
};
