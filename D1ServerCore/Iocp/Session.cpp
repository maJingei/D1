#include "Session.h"
#include "Service.h"
#include "SocketUtils.h"
#include <iostream>

namespace D1
{
	Session::Session()
	{
		// 4개 IocpEvent의 Owner를 this로 설정
		RecvIocpEvent.Owner = this;
		SendIocpEvent.Owner = this;
		ConnectIocpEvent.Owner = this;
		DisconnectIocpEvent.Owner = this;
	}

	Session::~Session()
	{
		if (Socket != INVALID_SOCKET)
		{
			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}
	}

	HANDLE Session::GetHandle()
	{
		return reinterpret_cast<HANDLE>(Socket);
	}

	void Session::Dispatch(IocpEvent* Event, int32 NumOfBytes)
	{
		switch (Event->Type)
		{
		case EventType::Connect:    ProcessConnect(); break;
		case EventType::Disconnect: ProcessDisconnect(); break;
		case EventType::Recv:       ProcessRecv(NumOfBytes); break;
		case EventType::Send:       ProcessSend(NumOfBytes); break;
		default: break;
		}
	}

	/*-----------------------------------------------------------------*/
	/*  I/O 등록                                                        */
	/*-----------------------------------------------------------------*/

	void Session::RegisterRecv()
	{
		if (bDisconnected) return;

		RecvIocpEvent.Init();

		WSABUF WsaBuf;
		WsaBuf.buf = RecvBuffer;
		WsaBuf.len = sizeof(RecvBuffer);

		DWORD Flags = 0;
		int32 Result = ::WSARecv(Socket, &WsaBuf, 1, nullptr, &Flags, &RecvIocpEvent, nullptr);
		if (Result == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Session] WSARecv failed: " << ::WSAGetLastError() << std::endl;
		}
	}

	void Session::RegisterSend(int32 NumOfBytes)
	{
		if (bDisconnected) return;

		SendIocpEvent.Init();

		WSABUF WsaBuf;
		WsaBuf.buf = SendBuffer;
		WsaBuf.len = NumOfBytes;

		int32 Result = ::WSASend(Socket, &WsaBuf, 1, nullptr, 0, &SendIocpEvent, nullptr);
		if (Result == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Session] WSASend failed: " << ::WSAGetLastError() << std::endl;
		}
	}

	void Session::RegisterConnect(const SOCKADDR_IN& Address)
	{
		if (bDisconnected) return;

		ConnectIocpEvent.Init();

		// ConnectEx는 사전 bind 필수
		SocketUtils::BindAnyAddress(Socket, 0);

		DWORD Bytes = 0;
		BOOL Result = SocketUtils::ConnectEx(Socket, reinterpret_cast<const SOCKADDR*>(&Address), sizeof(Address), nullptr, 0, &Bytes, &ConnectIocpEvent);
		if (Result == FALSE && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Session] ConnectEx failed: " << ::WSAGetLastError() << std::endl;
		}
	}

	/*-----------------------------------------------------------------*/
	/*  이벤트 처리 (내부)                                              */
	/*-----------------------------------------------------------------*/

	void Session::ProcessConnect()
	{
		if (bDisconnected) return;
		std::cout << "[Session] Connected" << std::endl;
		OnConnected();
	}

	void Session::ProcessDisconnect()
	{
		if (bDisconnected) return;
		bDisconnected = true;

		// ReleaseSession에서 Sessions에서 제거되어도 이 함수가 끝날 때까지 수명 보장
		std::shared_ptr<Session> Self = shared_from_this();

		if (Socket != INVALID_SOCKET)
		{
			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}
		OnDisconnected();
		if (std::shared_ptr<Service> Owner = OwnerService.lock())
		{
			Owner->ReleaseSession(Self);
		}
	}

	void Session::ProcessRecv(int32 NumOfBytes)
	{
		if (bDisconnected) return;
		// 연결 종료 감지
		if (NumOfBytes == 0)
		{
			ProcessDisconnect();
			return;
		}
		OnRecv(NumOfBytes);
	}

	void Session::ProcessSend(int32 NumOfBytes)
	{
		if (bDisconnected) return;
		OnSend(NumOfBytes);
	}

	/*-----------------------------------------------------------------*/
	/*  가상 훅 기본 구현                                               */
	/*-----------------------------------------------------------------*/

	void Session::OnConnected()
	{
		RegisterRecv();
	}

	void Session::OnRecv(int32 NumOfBytes)
	{
		// Echo: 수신 데이터를 그대로 송신
		::memcpy(SendBuffer, RecvBuffer, NumOfBytes);
		RegisterSend(NumOfBytes);
		// RegisterRecv는 OnSend에서 호출 (SendBuffer 덮어쓰기 방지)
	}

	void Session::OnSend(int32 NumOfBytes)
	{
		std::cout << "[Session] Sent " << NumOfBytes << " bytes" << std::endl;
		// 송신 완료 후 다음 수신 등록 (send/recv 직렬화)
		RegisterRecv();
	}

	void Session::OnDisconnected()
	{
		std::cout << "[Session] Disconnected" << std::endl;
	}

	/*-----------------------------------------------------------------*/
	/*  소켓 관리                                                       */
	/*-----------------------------------------------------------------*/

	void Session::SetSocket(SOCKET InSocket)
	{
		Socket = InSocket;
	}

	void Session::SetOwnerService(std::weak_ptr<Service> InService)
	{
		OwnerService = InService;
	}
}
