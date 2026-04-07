#pragma once

#include <winsock2.h>
#include "../Core/Types.h"

namespace D1
{
	class IocpObject;

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

		/** 이 이벤트를 소유한 객체 (비소유 포인터) */
		IocpObject* Owner = nullptr;
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

	/** 클라이언트 연결 수용 이벤트 */
	class AcceptEvent : public IocpEvent
	{
	public:
		AcceptEvent() : IocpEvent(EventType::Accept), ClientSocket(INVALID_SOCKET) {}

		/** AcceptEx용 미리 생성된 클라이언트 소켓 */
		SOCKET ClientSocket = INVALID_SOCKET;
	};

	/** 데이터 수신 이벤트 */
	class RecvEvent : public IocpEvent
	{
	public:
		RecvEvent() : IocpEvent(EventType::Recv) {}
	};

	/** 데이터 송신 이벤트 */
	class SendEvent : public IocpEvent
	{
	public:
		SendEvent() : IocpEvent(EventType::Send) {}
	};
}