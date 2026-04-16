#pragma once

#include <winsock2.h>
#include <memory>
#include <vector>
#include "Core/CoreMinimal.h"

class IocpObject;
class SendBuffer;

/** IOCP 이벤트 타입을 구분하는 열거형. */
enum class EventType : uint8
{
	Connect,
	Disconnect,
	Accept,
	Recv,
	Send,
};

/**
 * IOCP Overlapped I/O 이벤트의 베이스 클래스.
 * OVERLAPPED를 직접 상속하여 GQCS에서 static_cast로 안전하게 복원할 수 있다.
 */
class IocpEvent : public OVERLAPPED
{
public:
	IocpEvent(EventType InType);
	virtual ~IocpEvent() = default;

	/** OVERLAPPED 영역만 재초기화한다. Type, Owner는 유지된다. */
	void Init();

	IocpEvent(const IocpEvent&) = delete;
	IocpEvent& operator=(const IocpEvent&) = delete;
	IocpEvent(IocpEvent&&) = delete;
	IocpEvent& operator=(IocpEvent&&) = delete;

public:
	/** 이벤트 종류 */
	EventType Type;

	/**
	 * 이 이벤트의 비동기 I/O 수명을 연장하는 강참조.
	 * HoldForIo(Event)에서 set, IocpCore::Dispatch 진입 시 std::move로 reset.
	 * 따라서 존재 구간은 "I/O 게시 ~ 완료 Dispatch 진입"까지만이다.
	 */
	IocpObjectRef Owner;
};

/** 서버→외부 연결 요청 이벤트 */
class ConnectEvent : public IocpEvent
{
public:
	ConnectEvent() : IocpEvent(EventType::Connect) {}
};

/** 연결 해제 이벤트 */
class DisconnectEvent : public IocpEvent
{
public:
	DisconnectEvent() : IocpEvent(EventType::Disconnect) {}
};

/**
 * 클라이언트 연결 수용 이벤트.
 *
 * AcceptEx 는 호출 시점에 클라이언트 소켓을 이미 쥐고 있어야 하므로, Listener 는
 * PostAccept 시점에 Session 을 미리 생성해두고 그 소켓을 AcceptEx 에 넘긴다.
 * 즉 "대기 중인 AcceptEx 1개" 와 "pre-create 된 Session 1개" 가 쌍을 이루는 것이
 * 자연스러우므로, SessionRef 와 주소 버퍼를 AcceptEvent 가 함께 소유한다.
 * 소켓의 수명은 Session 소멸자가 책임지므로 이 이벤트는 명시적 closesocket 을 하지 않는다.
 */
class AcceptEvent : public IocpEvent
{
public:
	AcceptEvent() : IocpEvent(EventType::Accept) {}

	/**
	 * 이번 AcceptEx 완료 시 활성화될 pre-create Session.
	 * PostAccept 에서 생성(+Service.Sessions 에 등록), ProcessAccept 성공 경로에서 소비(std::move)된다.
	 * 셧다운/실패 시에는 Service::ReleaseSession 으로 Service 에서 제거 + reset 으로 Session 소멸자가 소켓을 닫는다.
	 */
	SessionRef Session;

	/** AcceptEx 가 로컬/원격 주소를 기록하는 버퍼. 비동기 완료까지 유지되어야 한다. */
	char AddrBuffer[64] = {};
};

/** 데이터 수신 이벤트 */
class RecvEvent : public IocpEvent
{
public:
	RecvEvent() : IocpEvent(EventType::Recv) {}
};

/**
 * 데이터 송신 이벤트.
 * WSASend가 진행 중인 동안 전송 대상 SendBuffer들의 shared_ptr을 보관하여
 * Chunk/버퍼 메모리의 생명주기를 연장한다. 송신 완료(ProcessSend) 시 clear.
 */
class SendEvent : public IocpEvent
{
public:
	SendEvent() : IocpEvent(EventType::Send) {}

	/** 현재 WSASend에 참여 중인 SendBuffer들. 완료 시 비워진다. */
	std::vector<SendBufferRef> SendBuffers;
};
