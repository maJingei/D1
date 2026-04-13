#include "Listener.h"
#include "Service.h"
#include "Session.h"
#include "SocketUtils.h"
#include <iostream>

namespace D1
{
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

	void Listener::Dispatch(IocpEvent* Event, int32 NumOfBytes)
	{
		assert(Event->Type == EventType::Accept);
		ProcessAccept();
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

		// IOCP에 Listener 등록
		if (LockedService->GetIocpCore()->Register(this) == false)
			return false;

		// 첫 번째 AcceptEx 게시
		PostAccept();

		std::cout << "[Listener] Started on " << Address.GetIp() << ":" << Address.GetPort() << std::endl;
		return true;
	}

	void Listener::Shutdown()
	{
		if (ListenSocket == INVALID_SOCKET)
			return;
		// closesocket은 pending AcceptEx를 WSA_OPERATION_ABORTED로 완료시킨다.
		// ListenSocket을 즉시 INVALID_SOCKET으로 돌려 ProcessAccept가 abort임을 감지하도록 한다.
		::closesocket(ListenSocket);
		ListenSocket = INVALID_SOCKET;
	}

	void Listener::ProcessAccept()
	{
		// 셧다운 중이면 abort된 AcceptEx 완료이므로 세션 생성/재게시를 건너뛴다.
		// 미리 할당한 클라이언트 소켓만 안전하게 해제한다.
		if (ListenSocket == INVALID_SOCKET)
		{
			if (AcceptIocpEvent.ClientSocket != INVALID_SOCKET)
			{
				::closesocket(AcceptIocpEvent.ClientSocket);
				AcceptIocpEvent.ClientSocket = INVALID_SOCKET;
			}
			return;
		}

		ServiceRef LockedService = OwnerService.lock();
		if (LockedService == nullptr)
		{
			::closesocket(AcceptIocpEvent.ClientSocket);
			AcceptIocpEvent.ClientSocket = INVALID_SOCKET;
			return;
		}

		// Step 1: Service의 Factory로 Session 생성
		SessionRef NewSession = LockedService->CreateSession();
		if (NewSession == nullptr)
		{
			::closesocket(AcceptIocpEvent.ClientSocket);
			PostAccept();
			return;
		}

		// Step 2: 소켓 설정
		NewSession->SetSocket(AcceptIocpEvent.ClientSocket);

		// Step 3: 클라이언트 소켓 컨텍스트 동기화
		SocketUtils::SetUpdateAcceptSocket(AcceptIocpEvent.ClientSocket, ListenSocket);

		// Step 4: Session을 IOCP에 등록
		LockedService->GetIocpCore()->Register(NewSession.get());

		// Step 5: 수신 대기 시작
		NewSession->RegisterRecv();

		std::cout << "[Listener] New client accepted (sessions: " << LockedService->GetSessionCount() << ")" << std::endl;

		// Step 6: 다음 AcceptEx 게시
		PostAccept();
	}

	void Listener::PostAccept()
	{
		// TODO : 여러개의 AcceptEvent를 통해 여러 세션을 받을 수 있도록 수정

		// HoldForIo가 Owner 세팅과 Event.Init()을 함께 수행한다.
		HoldForIo(AcceptIocpEvent);

		// 클라이언트 소켓 미리 생성
		AcceptIocpEvent.ClientSocket = SocketUtils::CreateTcpSocket();

		DWORD BytesReceived = 0;
		BOOL Result = SocketUtils::AcceptEx(ListenSocket, AcceptIocpEvent.ClientSocket, AcceptBuffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &BytesReceived, &AcceptIocpEvent);
		if (Result == FALSE && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Listener] AcceptEx failed: " << ::WSAGetLastError() << std::endl;
			// 게시 실패: HoldForIo가 잡아둔 self-ref + 미사용 수용 소켓을 즉시 해제한다.
			AcceptIocpEvent.Owner.reset();
			::closesocket(AcceptIocpEvent.ClientSocket);
			AcceptIocpEvent.ClientSocket = INVALID_SOCKET;
		}
	}
}