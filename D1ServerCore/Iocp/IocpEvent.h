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

/** IOCP Overlapped I/O 이벤트의 베이스 클래스. */
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

	/** 이 이벤트의 비동기 I/O 수명을 연장하는 강참조. */
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

/** 클라이언트 연결 수용 이벤트. */
class AcceptEvent : public IocpEvent
{
public:
	AcceptEvent() : IocpEvent(EventType::Accept) {}

	/** 이번 AcceptEx 완료 시 활성화될 pre-create Session. */
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

/** 데이터 송신 이벤트. */
class SendEvent : public IocpEvent
{
public:
	SendEvent() : IocpEvent(EventType::Send) {}

	/** 현재 WSASend에 참여 중인 SendBuffer들. 완료 시 비워진다. */
	std::vector<SendBufferRef> SendBuffers;
};
