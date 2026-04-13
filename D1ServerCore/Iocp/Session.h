#pragma once

#include <winsock2.h>
#include <memory>
#include <queue>
#include "IocpObject.h"
#include "IocpEvent.h"
#include "NetAddress.h"
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "../Threading/ReadWriteLock.h"

namespace D1
{
	class Service;

	/**
	 * 개별 클라이언트 연결을 대표하는 세션.
	 * IocpObject를 상속하여 Connect/Disconnect/Recv/Send 이벤트를 처리한다.
	 * 파생 클래스는 OnConnected/OnRecv/OnSend/OnDisconnected를 오버라이드하여
	 * 커스텀 동작을 구현한다. 기본 동작은 Echo.
	 *
	 * 반드시 std::make_shared로 생성해야 한다. 베이스 IocpObject가
	 * enable_shared_from_this를 이미 제공하므로 이중 상속은 하지 않는다
	 * (이중 상속 시 shared_from_this() ambiguous call 발생).
	 */
	class Session : public IocpObject
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

		/**
		 * SendBuffer를 송신 큐에 넣는다. 이미 전송 중이면 큐에 쌓이고,
		 * 그렇지 않으면 즉시 RegisterSend를 트리거한다.
		 * 여러 호출이 누적되면 하나의 WSASend가 Scatter-Gather로 한 번에 전송한다.
		 */
		void Send(SendBufferRef InSendBuffer);

		/** ConnectEx를 호출하여 서버에 연결한다. (클라이언트 측) */
		void RegisterConnect(const NetAddress& Address);

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

		/**
		 * 데이터 수신 시 호출된다. 파생 클래스는 [Data ~ Data+NumOfBytes) 범위를 파싱하고,
		 * 완전히 처리한 바이트 수를 반환한다. 미완성 패킷이 남아 있으면 그만큼은 반환하지 않아야
		 * 다음 수신에서 이어서 파싱할 수 있다.
		 *
		 * @param Data        RecvBuffer의 ReadPos 기준 시작 포인터
		 * @param NumOfBytes  현재 누적된 미처리 바이트 수
		 * @return            처리 완료 바이트 수 (ReadPos를 그만큼 전진)
		 */
		virtual int32 OnRecv(uint8* Data, int32 NumOfBytes);

		/** 데이터 송신 완료 시 호출된다. 기본 동작: RegisterRecv() */
		virtual void OnSend(int32 NumOfBytes);

		/** 연결 해제 시 호출된다. 기본 동작: 없음 */
		virtual void OnDisconnected();

		// 수신 스트림 누적 버퍼 (64KB, 커서 기반)
		RecvBuffer Recv;

	private:
		void ProcessConnect();
		void ProcessDisconnect();
		void ProcessRecv(int32 NumOfBytes);
		void ProcessSend(int32 NumOfBytes);

		/** 송신 큐에서 모아둔 SendBuffer 전체를 WSABUF 배열로 묶어 한 번에 전송한다. */
		void RegisterSend();

		SOCKET Socket = INVALID_SOCKET;
		bool bDisconnected = false;
		std::weak_ptr<Service> OwnerService;

		/*-----------------------------------------------------------------*/
		/*  Send 경로 상태 (Scatter-Gather 모델)                            */
		/*-----------------------------------------------------------------*/

		/** 송신 대기 중인 SendBuffer 큐. */
		std::queue<SendBufferRef> SendQueue;

		/** 큐/플래그 보호용. WSASend 직전까지만 잡는다. */
		ReadWriteLock SendLock;

		/**
		 * 이미 RegisterSend가 진행 중인지 표시하는 플래그.
		 * true인 동안 Send()는 큐에 적재만 하고 WSASend를 재트리거하지 않는다.
		 * ProcessSend 완료 시 큐 상태를 보고 false로 내리거나 연쇄 RegisterSend.
		 */
		bool bSendRegistered = false;
		// TODO: 락프리 큐로 전환하여 Send 경로 병목 제거. 현재는 구현 난이도 때문에 WriteLock 사용.

		// 4개 IocpEvent 멤버 (각 타입별 1개)
		RecvEvent RecvIocpEvent;
		SendEvent SendIocpEvent;
		ConnectEvent ConnectIocpEvent;
		DisconnectEvent DisconnectIocpEvent;
	};
}
