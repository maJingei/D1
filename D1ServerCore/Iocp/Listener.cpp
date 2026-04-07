#include "Listener.h"
#include "IocpCore.h"
#include "Session.h"
#include "SocketUtils.h"
#include <iostream>

namespace D1
{
	Listener::Listener()
	{
		AcceptIocpEvent.Owner = this;
	}

	Listener::~Listener()
	{
		CloseAllSessions();

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

	bool Listener::Start(const SOCKADDR_IN& Address, IocpCore* Core)
	{
		CoreRef = Core;

		// 소켓 생성
		ListenSocket = SocketUtils::CreateTcpSocket();
		if (ListenSocket == INVALID_SOCKET)
			return false;

		// 소켓 옵션
		SocketUtils::SetReuseAddress(ListenSocket, true);
		SocketUtils::SetLinger(ListenSocket, 0, 0);

		// Bind + Listen
		if (SocketUtils::Bind(ListenSocket, Address) == false)
			return false;

		if (SocketUtils::Listen(ListenSocket) == false)
			return false;

		// IOCP에 Listener 등록
		if (CoreRef->Register(this) == false)
			return false;

		// 첫 번째 AcceptEx 게시
		PostAccept();

		std::cout << "[Listener] Started on port " << ::ntohs(Address.sin_port) << std::endl;
		return true;
	}

	void Listener::ProcessAccept()
	{
		// Step 1: Session 생성 및 소켓 설정
		Session* NewSession = new Session();
		NewSession->SetSocket(AcceptIocpEvent.ClientSocket);

		// Step 2: 클라이언트 소켓 컨텍스트 동기화
		SocketUtils::SetUpdateAcceptSocket(AcceptIocpEvent.ClientSocket, ListenSocket);

		// Step 3: Session을 IOCP에 등록
		CoreRef->Register(NewSession);

		// Step 4: 세션 관리
		Sessions.insert(NewSession);

		// Step 5: 수신 대기 시작
		NewSession->RegisterRecv();

		std::cout << "[Listener] New client accepted (sessions: " << Sessions.size() << ")" << std::endl;

		// Step 6: 다음 AcceptEx 게시
		PostAccept();
	}

	void Listener::PostAccept()
	{
		// OVERLAPPED 재초기화
		AcceptIocpEvent.Init();

		// 클라이언트 소켓 미리 생성
		AcceptIocpEvent.ClientSocket = SocketUtils::CreateTcpSocket();

		DWORD BytesReceived = 0;
		BOOL Result = SocketUtils::AcceptEx(ListenSocket, AcceptIocpEvent.ClientSocket, AcceptBuffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &BytesReceived, &AcceptIocpEvent);
		if (Result == FALSE && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Listener] AcceptEx failed: " << ::WSAGetLastError() << std::endl;
		}
	}

	void Listener::CloseAllSessions()
	{
		// TODO: 추후 ServerService로 이전
		for (Session* Sess : Sessions)
		{
			delete Sess;
		}
		Sessions.clear();

		std::cout << "[Listener] All sessions closed" << std::endl;
	}
}