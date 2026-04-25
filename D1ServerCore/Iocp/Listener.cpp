#include "Listener.h"
#include "Service.h"
#include "Session.h"
#include "SocketUtils.h"
#include <iostream>

Listener::Listener() = default;

Listener::~Listener()
{
	if (ListenSocket != INVALID_SOCKET)
	{
		::closesocket(ListenSocket);
		ListenSocket = INVALID_SOCKET;
	}
}

HANDLE Listener::GetHandle()
{
	return reinterpret_cast<HANDLE>(ListenSocket);
}

bool Listener::Start(const NetAddress& Address, std::weak_ptr<Service> InService)
{
	OwnerService = InService;

	ServiceRef LockedService = OwnerService.lock();
	if (LockedService == nullptr)
		return false;

	// 소켓 생성
	ListenSocket = SocketUtils::CreateTcpSocket();
	if (ListenSocket == INVALID_SOCKET)
		return false;

	// 소켓 옵션
	SocketUtils::SetReuseAddress(ListenSocket, true);
	SocketUtils::SetLinger(ListenSocket, 0, 0);

	// Bind + Listen
	if (SocketUtils::Bind(ListenSocket, Address.GetSockAddr()) == false)
		return false;
	if (SocketUtils::Listen(ListenSocket) == false)
		return false;

	// IOCP 에 Listener 등록
	if (LockedService->GetIocpCore()->Register(this) == false)
		return false;

	// AcceptEx 풀 전체를 미리 게시해 동시 수용 용량을 확보한다.
	for (AcceptEvent& Slot : AcceptEvents)
		PostAccept(Slot);

	return true;
}

void Listener::Shutdown()
{
	if (ListenSocket == INVALID_SOCKET)
		return;
	// closesocket 은 pending AcceptEx 들을 WSA_OPERATION_ABORTED 로 완료시킨다.
	// ListenSocket 을 즉시 INVALID_SOCKET 으로 돌려 ProcessAccept 가 abort 임을 감지하도록 한다.
	::closesocket(ListenSocket);
	ListenSocket = INVALID_SOCKET;
}

void Listener::Dispatch(IocpEvent* Event, int32 NumOfBytes)
{
	// Accept 타입이 아닐 가능성은 설계상 없지만, Release 에서 assert 가 사라지므로 런타임 가드로 처리한다.
	if (Event == nullptr || Event->Type != EventType::Accept)
		return;

	// 완료된 AcceptEx 가 풀 내 어느 슬롯에 속하는지 포인터 비교로 찾는다.
	// (16개 선형 스캔 — 슬롯마다 인덱스를 Event 에 심어두는 것보다 간단하고 비용 무시 가능)
	AcceptEvent* CompletedEvent = static_cast<AcceptEvent*>(Event);
	for (AcceptEvent& Slot : AcceptEvents)
	{
		if (&Slot == CompletedEvent)
		{
			ProcessAccept(Slot);
			return;
		}
	}
}	

void Listener::ProcessAccept(AcceptEvent& Event)
{
	// 셧다운 중이면 abort 된 AcceptEx 완료이므로 pre-Session 을 정리하고 재게시를 생략한다.
	if (ListenSocket == INVALID_SOCKET)
	{
		DropPendingSession(Event);
		return;
	}

	ServiceRef LockedService = OwnerService.lock();
	if (LockedService == nullptr)
	{
		DropPendingSession(Event);
		return;
	}

	// pre-Session 을 슬롯에서 꺼낸다. 이 시점부터 Session 수명은 아래 finalize 경로가 책임진다.
	SessionRef AcceptedSession = std::move(Event.Session);
	if (AcceptedSession == nullptr)
	{
		// 비정상 — PostAccept 를 거치지 않은 Event 가 완료됐다. 슬롯만 재무장.
		PostAccept(Event);
		return;
	}

	// Step 1: 클라이언트 소켓 컨텍스트 동기화 (getpeername 등이 정상 동작하도록)
	SocketUtils::SetUpdateAcceptSocket(AcceptedSession->GetSocket(), ListenSocket);

	// Step 2: Session 을 IOCP 에 등록
	LockedService->GetIocpCore()->Register(AcceptedSession.get());

	// Step 3: 수신 대기 시작
	AcceptedSession->RegisterRecv();

	// Step 4: 같은 슬롯을 새로운 pre-Session 으로 재무장하여 풀 깊이 유지
	PostAccept(Event);
}

void Listener::PostAccept(AcceptEvent& Event, int32 RetryBudget)
{
	// 셧다운 중이면 재게시하지 않는다. 슬롯은 그대로 비워둔다.
	if (ListenSocket == INVALID_SOCKET)
		return;

	ServiceRef LockedService = OwnerService.lock();
	if (LockedService == nullptr)
		return;

	// 이전 post 의 잔존 Session 이 있으면 정리. (정상 경로에서는 ProcessAccept 가 move 로 이미 비워뒀어야 함)
	if (Event.Session)
		DropPendingSession(Event);

	// pre-Session 생성 — Service.Sessions 에 등록된다. AcceptEx 실패 시 DropPendingSession 으로 되돌린다.
	SessionRef NewSession = LockedService->CreateSession();
	if (NewSession == nullptr)
		return;

	NewSession->SetSocket(SocketUtils::CreateTcpSocket());
	if (NewSession->GetSocket() == INVALID_SOCKET)
	{
		LockedService->ReleaseSession(NewSession);
		return;
	}

	Event.Session = NewSession;

	// HoldForIo 가 Owner 세팅과 Event.Init() 을 함께 수행한다.
	HoldForIo(Event);

	DWORD BytesReceived = 0;
	BOOL Result = SocketUtils::AcceptEx(ListenSocket, NewSession->GetSocket(), Event.AddrBuffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &BytesReceived, &Event);
	if (Result == FALSE && ::WSAGetLastError() != WSA_IO_PENDING)
	{
		std::cout << "[Listener] AcceptEx failed: " << ::WSAGetLastError() << "\n";
		// 실패한 post 의 self-ref 와 pre-Session 을 정리하고, 슬롯을 살리기 위해 한 번만 재시도한다.
		Event.Owner.reset();
		DropPendingSession(Event);
		if (RetryBudget > 0)
			PostAccept(Event, RetryBudget - 1);
	}
}

void Listener::DropPendingSession(AcceptEvent& Event)
{
	if (Event.Session == nullptr)
		return;
	ServiceRef LockedService = OwnerService.lock();
	if (LockedService)
		LockedService->ReleaseSession(Event.Session);
	Event.Session.reset();
}
