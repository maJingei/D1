#pragma once

#include <winsock2.h>
#include "IocpObject.h"
#include "IocpEvent.h"

namespace D1
{
	/**
	 * 개별 클라이언트 연결을 대표하는 세션.
	 * IocpObject를 상속하여 Connect/Disconnect/Recv/Send 이벤트를 처리한다.
	 * Echo 서버에서는 수신 데이터를 그대로 송신한다.
	 */
	class Session : public IocpObject
	{
	public:
		Session();
		~Session();

		// IocpObject 인터페이스
		HANDLE GetHandle() override;
		void Dispatch(IocpEvent* Event, int32 NumOfBytes) override;

		/*-----------------------------------------------------------------*/
		/*  I/O 등록                                                        */
		/*-----------------------------------------------------------------*/

		/** WSARecv를 등록하여 수신 대기를 시작한다. */
		void RegisterRecv();

		/** WSASend를 등록하여 데이터를 전송한다. */
		void RegisterSend(int32 NumOfBytes);

		/** ConnectEx를 호출하여 서버에 연결한다. (클라이언트 측) */
		void RegisterConnect(const SOCKADDR_IN& Address);

		/*-----------------------------------------------------------------*/
		/*  이벤트 처리                                                     */
		/*-----------------------------------------------------------------*/

		void ProcessConnect();
		void ProcessDisconnect();
		void ProcessRecv(int32 NumOfBytes);
		void ProcessSend(int32 NumOfBytes);

		/*-----------------------------------------------------------------*/
		/*  소켓 관리                                                       */
		/*-----------------------------------------------------------------*/

		/** 세션 소켓을 설정한다. (Listener::ProcessAccept에서 호출) */
		void SetSocket(SOCKET InSocket);
		SOCKET GetSocket() const { return Socket; }

		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;
		Session(Session&&) = delete;
		Session& operator=(Session&&) = delete;

	private:
		SOCKET Socket = INVALID_SOCKET;
		bool bDisconnected = false;

		// 4개 IocpEvent 멤버 (각 타입별 1개)
		RecvEvent RecvIocpEvent;
		SendEvent SendIocpEvent;
		ConnectEvent ConnectIocpEvent;
		DisconnectEvent DisconnectIocpEvent;

		// 임시 고정 버퍼 (추후 SendBuffer/RecvBuffer 전용 클래스로 교체)
		char RecvBuffer[4096] = {};
		char SendBuffer[4096] = {};
	};
}