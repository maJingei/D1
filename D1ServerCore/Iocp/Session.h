#pragma once

#include <winsock2.h>
#include <memory>
#include <queue>
#include "IocpObject.h"
#include "IocpEvent.h"
#include "NetAddress.h"
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "../Job/JobSerializer.h"

namespace D1
{
	class Service;
	class Session;

	/**
	 * Session 전용 Job 직렬화기.
	 * Session 이 멤버로 보유하며, Session::Send / ProcessSend 를
	 * FlushWorker 에서 순차 실행하도록 직렬화한다.
	 *
	 * enable_shared_from_this<FSessionSerializer> 단일 계보 — 충돌 없음.
	 * GetSerializerRef() 는 자기 자신(this shared_ptr) 을 반환하므로
	 * GlobalJobQueue 등록에 필요한 shared_ptr<JobSerializer> 를 올바르게 공급한다.
	 */
	class FSessionSerializer : public JobSerializer, public std::enable_shared_from_this<FSessionSerializer>
	{
	public:
		explicit FSessionSerializer(std::weak_ptr<Session> InSession) : Owner(InSession) {}

		std::weak_ptr<Session> Owner;

	protected:
		JobSerializerRef GetSerializerRef() override
		{
			return std::static_pointer_cast<JobSerializer>(shared_from_this());
		}
	};

	/**
	 * 개별 클라이언트 연결을 대표하는 세션.
	 * IocpObject 를 상속하여 Connect/Disconnect/Recv/Send 이벤트를 처리한다.
	 *
	 * Send 경로 직렬화 모델:
	 *   - Send()        : Job 을 Serializer(FSessionSerializer) 에 Push (즉시 반환).
	 *   - DoSend()      : 실제 SendQueue push + bSendRegistered 분기 + RegisterSend 호출.
	 *                     항상 FlushJob 컨텍스트에서 실행 — 별도 락 불필요.
	 *   - ProcessSend() : IOCP 완료 콜백 — DoProcessSend 를 Job 으로 래핑해 직렬화 유지.
	 *   - DoProcessSend(): bSendRegistered 해제 및 잔여 큐 존재 시 RegisterSend 연쇄 호출.
	 *
	 * 반드시 std::make_shared 로 생성해야 한다.
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
		 * SendBuffer 를 Serializer Job 으로 래핑하여 Push 한다.
		 * 실제 송신 처리는 DoSend 에서 수행되며, 같은 세션에 대한 Send 순서가 보장된다.
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

		/** 데이터 송신 완료 시 호출된다. 기본 동작: 없음 */
		virtual void OnSend(int32 NumOfBytes);

		/** 연결 해제 시 호출된다. 기본 동작: 없음 */
		virtual void OnDisconnected();

		// 수신 스트림 누적 버퍼 (64KB, 커서 기반)
		RecvBuffer Recv;

	private:
		void ProcessConnect();
		void ProcessDisconnect();
		void ProcessRecv(int32 NumOfBytes);

		/**
		 * IOCP Send 완료 콜백. DoProcessSend 를 Job 으로 래핑하여
		 * Serializer 직렬화 파이프라인 안에서 처리한다.
		 */
		void ProcessSend(int32 NumOfBytes);

		/** 송신 큐에서 모아둔 SendBuffer 전체를 WSABUF 배열로 묶어 한 번에 전송한다. */
		void RegisterSend();

		/**
		 * Send 실제 처리 — FlushJob 컨텍스트에서만 호출된다.
		 * SendQueue push + bSendRegistered 분기 + RegisterSend 호출. 락 없음.
		 */
		void DoSend(SendBufferRef InSendBuffer);

		/**
		 * ProcessSend 실제 처리 — FlushJob 컨텍스트에서만 호출된다.
		 * bSendRegistered 해제 및 잔여 큐 존재 시 RegisterSend 연쇄 호출. 락 없음.
		 */
		void DoProcessSend(int32 NumOfBytes);

		SOCKET Socket = INVALID_SOCKET;
		bool bDisconnected = false;
		std::weak_ptr<Service> OwnerService;

		/*-----------------------------------------------------------------*/
		/*  Send 경로 직렬화기                                              */
		/*-----------------------------------------------------------------*/

		/**
		 * Session 전용 JobSerializer 인스턴스.
		 * Session 생성 시 make_shared 로 함께 생성되며, Send/ProcessSend 경로를
		 * FlushWorker 에서 순차 실행되도록 직렬화한다.
		 */
		std::shared_ptr<FSessionSerializer> Serializer;

		/*-----------------------------------------------------------------*/
		/*  Send 경로 상태 (Scatter-Gather 모델)                            */
		/*-----------------------------------------------------------------*/

		/** 송신 대기 중인 SendBuffer 큐. Serializer 직렬화로 보호 — 별도 락 없음. */
		std::queue<SendBufferRef> SendQueue;

		/**
		 * 이미 RegisterSend가 진행 중인지 표시하는 플래그.
		 * true인 동안 DoSend()는 큐에 적재만 하고 WSASend를 재트리거하지 않는다.
		 * DoProcessSend 완료 시 큐 상태를 보고 false로 내리거나 연쇄 RegisterSend.
		 * Serializer 직렬화로 보호 — 별도 락 없음.
		 */
		bool bSendRegistered = false;

		// 4개 IocpEvent 멤버 (각 타입별 1개)
		RecvEvent RecvIocpEvent;
		SendEvent SendIocpEvent;
		ConnectEvent ConnectIocpEvent;
		DisconnectEvent DisconnectIocpEvent;
	};
}