#pragma once

#include <winsock2.h>
#include <memory>
#include <queue>
#include <atomic>
#include <mutex>
#include "Iocp/IocpObject.h"
#include "Iocp/IocpEvent.h"
#include "Iocp/NetAddress.h"
#include "Iocp/RecvBuffer.h"
#include "Iocp/SendBuffer.h"

class Service;

/** 개별 클라이언트 연결을 대표하는 세션. */
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

	/** SendBuffer 송신 예약. 내부 SendLock 보호 하에 즉시 DoSend 본문을 실행한다. 파생 클래스 로깅 hook 용 virtual. */
	virtual void Send(SendBufferRef InSendBuffer);

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

	/** 데이터 수신 시 호출된다. */
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

	/** IOCP Send 완료 콜백. */
	void ProcessSend(int32 NumOfBytes);

	/** 송신 큐에서 모아둔 SendBuffer 전체를 WSABUF 배열로 묶어 한 번에 전송한다. SendLock 보호 하에서만 호출된다. */
	void RegisterSend();

	SOCKET Socket = INVALID_SOCKET;

	/** 연결 종료 신호용 atomic 플래그. */
	std::atomic<bool> bDisconnected{false};

	/** ProcessDisconnect 테어다운 1회 보장용 atomic 가드. */
	std::atomic<bool> bTeardownDone{false};

	std::weak_ptr<Service> OwnerService;

	/*-----------------------------------------------------------------*/
	/*  Send 경로 동기화                                                */
	/*-----------------------------------------------------------------*/

	/** Send 경로(SendQueue, bSendRegistered, RegisterSend/WSASend) 접근을 보호하는 mutex. */
	std::mutex SendLock;

	/*-----------------------------------------------------------------*/
	/*  Send 경로 상태 (Scatter-Gather 모델)                            */
	/*-----------------------------------------------------------------*/

	/** 송신 대기 중인 SendBuffer 큐. SendLock 보호. */
	std::queue<SendBufferRef> SendQueue;

	/** 이미 RegisterSend 가 진행 중인지 표시하는 플래그. */
	bool bSendRegistered = false;

	// 4개 IocpEvent 멤버 (각 타입별 1개)
	RecvEvent RecvIocpEvent;
	SendEvent SendIocpEvent;
	ConnectEvent ConnectIocpEvent;
	DisconnectEvent DisconnectIocpEvent;
};
