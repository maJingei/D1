#pragma once

#include <winsock2.h>
#include <memory>
#include "Core/CoreMinimal.h"

class IocpEvent;

/** IOCP Dispatch를 위한 추상 베이스 클래스. */
class IocpObject : public std::enable_shared_from_this<IocpObject>
{
public:
	virtual ~IocpObject() = default;

	/** IOCP에 등록할 핸들을 반환한다. (Session: SOCKET, Listener: 리슨 소켓) */
	virtual HANDLE GetHandle() = 0;

	/** GQCS 완료 통지를 처리한다. */
	virtual void Dispatch(IocpEvent* Event, int32 NumOfBytes) = 0;

	IocpObject(const IocpObject&) = delete;
	IocpObject& operator=(const IocpObject&) = delete;
	IocpObject(IocpObject&&) = delete;
	IocpObject& operator=(IocpObject&&) = delete;

protected:
	IocpObject() = default;

	/** 비동기 I/O 게시 직전에 호출하여 자기 shared_ptr을 Event에 바인딩한다. */
	void HoldForIo(IocpEvent& Event);
};
