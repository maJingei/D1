#pragma once

#include <winsock2.h>
#include <memory>
#include <queue>
#include <atomic>
#include "Iocp/IocpObject.h"
#include "Iocp/IocpEvent.h"
#include "Iocp/NetAddress.h"
#include "Iocp/RecvBuffer.h"
#include "Iocp/SendBuffer.h"
#include "Job/JobQueue.h"

class Service;

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
	 * SendBuffer 를 SendSerializer Job 으로 래핑하여 Push 한다.
	 * 같은 세션에 대한 Send 순서가 FIFO 로 보장되며, FlushWorker 가 병렬 분산 실행한다.
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
	 * IOCP Send 완료 콜백. DoProcessSend 를 Job 으로 래핑하여 SendSerializer 에 Push.
	 */
	void ProcessSend(int32 NumOfBytes);

	/** 송신 큐에서 모아둔 SendBuffer 전체를 WSABUF 배열로 묶어 한 번에 전송한다. SendSerializer 직렬화 컨텍스트 전용. */
	void RegisterSend();

	/**
	 * Send 실제 처리 — SendSerializer FlushJob 컨텍스트에서만 호출된다.
	 * SendQueue push + bSendRegistered 분기 + RegisterSend 호출. 락 없음.
	 */
	void DoSend(SendBufferRef InSendBuffer);

	/**
	 * ProcessSend 실제 처리 — SendSerializer FlushJob 컨텍스트에서만 호출된다.
	 * bSendRegistered 해제 및 잔여 큐 존재 시 RegisterSend 연쇄 호출. 락 없음.
	 */
	void DoProcessSend(int32 NumOfBytes);

	SOCKET Socket = INVALID_SOCKET;

	/**
	 * 연결 종료 신호용 atomic 플래그.
	 * ProcessDisconnect 및 RegisterSend 에러 경로에서 true 로 set.
	 * Send 진입부에서 체크하여 끊긴 세션으로의 Send 를 스킵한다.
	 */
	std::atomic<bool> bDisconnected{false};

	/**
	 * ProcessDisconnect 테어다운 1회 보장용 atomic 가드.
	 * compare_exchange 로 false→true 에 성공한 호출만 실제 teardown(소켓 close, OnDisconnected, ReleaseSession)을 수행한다.
	 */
	std::atomic<bool> bTeardownDone{false};

	std::weak_ptr<Service> OwnerService;

	/*-----------------------------------------------------------------*/
	/*  Send 경로 직렬화기                                              */
	/*-----------------------------------------------------------------*/

	/**
	 * Session 전용 JobQueue.
	 * Send/ProcessSend 를 Job 으로 래핑해 FlushWorker 에서 직렬 실행한다.
	 * 세션마다 JobQueue 가 독립적으로 GlobalJobQueue 에 등록되므로 여러 세션의 Send 가 워커 풀 전역에 분산된다.
	 */
	std::shared_ptr<JobQueue> SendSerializer;

	/*-----------------------------------------------------------------*/
	/*  Send 경로 상태 (Scatter-Gather 모델)                            */
	/*-----------------------------------------------------------------*/

	/** 송신 대기 중인 SendBuffer 큐. SendSerializer 직렬화로 보호 — 별도 락 없음. */
	std::queue<SendBufferRef> SendQueue;

	/**
	 * 이미 RegisterSend 가 진행 중인지 표시하는 플래그.
	 * DoSend/DoProcessSend 가 SendSerializer 직렬화 안에서만 실행되므로 atomic 불필요.
	 */
	bool bSendRegistered = false;

	// 4개 IocpEvent 멤버 (각 타입별 1개)
	RecvEvent RecvIocpEvent;
	SendEvent SendIocpEvent;
	ConnectEvent ConnectIocpEvent;
	DisconnectEvent DisconnectIocpEvent;
};
