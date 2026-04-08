#pragma once

#include <winsock2.h>
#include <memory>
#include "IocpObject.h"
#include "IocpEvent.h"

namespace D1
{
	class Service;

	/**
	 * 개별 클라이언트 연결을 대표하는 세션.
	 * IocpObject를 상속하여 Connect/Disconnect/Recv/Send 이벤트를 처리한다.
	 * 파생 클래스는 OnConnected/OnRecv/OnSend/OnDisconnected를 오버라이드하여
	 * 커스텀 동작을 구현한다. 기본 동작은 Echo.
	 *
	 * 반드시 std::make_shared로 생성해야 한다. (enable_shared_from_this 요구사항)
	 */
	class Session : public IocpObject, public std::enable_shared_from_this<Session>
	{
	public:
		Session();
		virtual ~Session();

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
		/*  소켓 관리                                                       */
		/*-----------------------------------------------------------------*/

		/** 세션 소켓을 설정한다. (Listener::ProcessAccept에서 호출) */
		void SetSocket(SOCKET InSocket);
		SOCKET GetSocket() const { return Socket; }

		/** 소유 Service를 설정한다. (weak_ptr로 비소유 참조) */
		void SetOwnerService(std::weak_ptr<Service> InService);
		std::weak_ptr<Service> GetOwnerService() const { return OwnerService; }

		/** 연결 해제 여부를 반환한다. */
		bool IsDisconnected() const { return bDisconnected; }

		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;
		Session(Session&&) = delete;
		Session& operator=(Session&&) = delete;

	protected:
		/*-----------------------------------------------------------------*/
		/*  파생 클래스용 가상 훅                                            */
		/*-----------------------------------------------------------------*/

		/** 연결 완료 후 호출된다. 기본 동작: RegisterRecv() */
		virtual void OnConnected();

		/** 데이터 수신 시 호출된다. 기본 동작: Echo (RecvBuffer → SendBuffer → RegisterSend) */
		virtual void OnRecv(int32 NumOfBytes);

		/** 데이터 송신 완료 시 호출된다. 기본 동작: RegisterRecv() */
		virtual void OnSend(int32 NumOfBytes);

		/** 연결 해제 시 호출된다. 기본 동작: 없음 */
		virtual void OnDisconnected();

		// 임시 고정 버퍼 (추후 SendBuffer/RecvBuffer 전용 클래스로 교체)
		char RecvBuffer[4096] = {};
		char SendBuffer[4096] = {};

	private:
		void ProcessConnect();
		void ProcessDisconnect();
		void ProcessRecv(int32 NumOfBytes);
		void ProcessSend(int32 NumOfBytes);

		SOCKET Socket = INVALID_SOCKET;
		bool bDisconnected = false;
		std::weak_ptr<Service> OwnerService;

		// 4개 IocpEvent 멤버 (각 타입별 1개)
		RecvEvent RecvIocpEvent;
		SendEvent SendIocpEvent;
		ConnectEvent ConnectIocpEvent;
		DisconnectEvent DisconnectIocpEvent;
	};
}
