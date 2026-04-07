#include "Session.h"
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
	/*  이벤트 처리                                                     */
	/*-----------------------------------------------------------------*/

	void Session::ProcessConnect()
	{
		if (bDisconnected) return;

		std::cout << "[Session] Connected" << std::endl;

		// 연결 완료 후 수신 대기 시작
		RegisterRecv();
	}

	void Session::ProcessDisconnect()
	{
		if (bDisconnected) return;

		bDisconnected = true;
		std::cout << "[Session] Disconnected" << std::endl;

		if (Socket != INVALID_SOCKET)
		{
			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}
	}

	void Session::ProcessRecv(int32 NumOfBytes)
	{
		if (bDisconnected) return;

		// 연결 종료 감지
		if (NumOfBytes == 0)
		{
			bDisconnected = true;
			std::cout << "[Session] Client disconnected (recv 0)" << std::endl;
			if (Socket != INVALID_SOCKET)
			{
				::closesocket(Socket);
				Socket = INVALID_SOCKET;
			}
			return;
		}

		// Echo: 수신 데이터를 그대로 송신
		::memcpy(SendBuffer, RecvBuffer, NumOfBytes);
		RegisterSend(NumOfBytes);
		// RegisterRecv는 ProcessSend에서 호출 (SendBuffer 덮어쓰기 방지)
	}

	void Session::ProcessSend(int32 NumOfBytes)
	{
		if (bDisconnected) return;

		std::cout << "[Session] Sent " << NumOfBytes << " bytes" << std::endl;

		// 송신 완료 후 다음 수신 등록 (send/recv 직렬화)
		RegisterRecv();
	}

	/*-----------------------------------------------------------------*/
	/*  소켓 관리                                                       */
	/*-----------------------------------------------------------------*/

	void Session::SetSocket(SOCKET InSocket)
	{
		Socket = InSocket;
	}
}