#pragma once

#include <winsock2.h>
#include <memory>
#include "Core/CoreMinimal.h"

class IocpEvent;

/**
 * IOCP Dispatch를 위한 추상 베이스 클래스.
 * Listener, Session 등이 상속받아 완료 통지를 처리한다.
 *
 * enable_shared_from_this<IocpObject>를 상속하여 I/O 게시~완료 구간 동안
 * IocpEvent::Owner(shared_ptr)로 자기 수명을 연장한다. 이로 인해 모든
 * IocpObject 파생은 반드시 std::make_shared로 생성되어야 하며 스택/값-멤버
 * 할당은 허용되지 않는다 (infectious tax 수용).
 */
class IocpObject : public std::enable_shared_from_this<IocpObject>
{
public:
	virtual ~IocpObject() = default;

	/** IOCP에 등록할 핸들을 반환한다. (Session: SOCKET, Listener: 리슨 소켓) */
	virtual HANDLE GetHandle() = 0;

	/**
	 * GQCS 완료 통지를 처리한다.
	 *
	 * @param Event        완료된 IocpEvent
	 * @param NumOfBytes   전송된 바이트 수
	 */
	virtual void Dispatch(IocpEvent* Event, int32 NumOfBytes) = 0;

	IocpObject(const IocpObject&) = delete;
	IocpObject& operator=(const IocpObject&) = delete;
	IocpObject(IocpObject&&) = delete;
	IocpObject& operator=(IocpObject&&) = delete;

protected:
	IocpObject() = default;

	/**
	 * 비동기 I/O 게시 직전에 호출하여 자기 shared_ptr을 Event에 바인딩한다.
	 * I/O 완료 후 IocpCore::Dispatch가 Event->Owner를 std::move로 빼내
	 * KeepAlive 스택 로컬로 옮겨 수명을 보장한다.
	 *
	 * @param Event   대상 IocpEvent. OVERLAPPED 재초기화도 함께 수행된다.
	 */
	void HoldForIo(IocpEvent& Event);
};
