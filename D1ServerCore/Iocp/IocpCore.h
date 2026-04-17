#pragma once

#include <winsock2.h>
#include "Core/CoreMinimal.h"

class IocpObject;

/** IOCP CompletionPort를 관리하는 Thin Wrapper. */
class IocpCore
{
public:
	IocpCore();
	~IocpCore();

	/** IOCP CompletionPort 핸들을 생성한다. */
	bool Initialize();

	/** IocpObject를 CompletionPort에 등록한다. */
	bool Register(IocpObject* Object);

	/** GetQueuedCompletionStatus를 1회 호출하여 완료된 I/O를 처리한다. */
	bool Dispatch(uint32 TimeoutMs = INFINITE);

	/** IOCP 핸들을 반환한다. */
	HANDLE GetHandle() const { return IocpHandle; }

	IocpCore(const IocpCore&) = delete;
	IocpCore& operator=(const IocpCore&) = delete;
	IocpCore(IocpCore&&) = delete;
	IocpCore& operator=(IocpCore&&) = delete;

private:
	HANDLE IocpHandle;
};
