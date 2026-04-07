#include "IocpCore.h"
#include "IocpObject.h"
#include "IocpEvent.h"

namespace D1
{
	IocpCore::IocpCore() : IocpHandle(INVALID_HANDLE_VALUE)
	{
	}

	IocpCore::~IocpCore()
	{
		if (IocpHandle != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(IocpHandle);
			IocpHandle = INVALID_HANDLE_VALUE;
		}
	}

	bool IocpCore::Initialize()
	{
		// 이중 초기화 방지
		assert(IocpHandle == INVALID_HANDLE_VALUE);

        // null 을 보내면 커널에서 Io port의 Queue가 생성
		IocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		return IocpHandle != NULL;
	}

	bool IocpCore::Register(IocpObject* Object)
	{
		HANDLE Result = ::CreateIoCompletionPort(Object->GetHandle(), IocpHandle, reinterpret_cast<ULONG_PTR>(Object), 0);
		return Result == IocpHandle;
	}

	bool IocpCore::Dispatch(uint32 TimeoutMs)
	{
		DWORD NumOfBytes = 0;
		ULONG_PTR Key = 0;
		OVERLAPPED* Overlapped = nullptr;

		BOOL bSuccess = ::GetQueuedCompletionStatus(IocpHandle, &NumOfBytes, &Key, &Overlapped, TimeoutMs);

		// 종료 신호: PQCS(0, 0, nullptr)가 성공적으로 dequeue된 경우에만 종료
		// bSuccess 체크 필수: GQCS 타임아웃 시 Key==0이지만 bSuccess==FALSE이므로 구분 가능
		if (bSuccess == TRUE && Key == 0 && Overlapped == nullptr)
		{
			return false;
		}

		if (bSuccess == TRUE && Overlapped != nullptr)
		{
			// 성공: OVERLAPPED → IocpEvent, Key → IocpObject
			IocpEvent* Event = static_cast<IocpEvent*>(Overlapped);
			IocpObject* Object = reinterpret_cast<IocpObject*>(Key);
			Object->Dispatch(Event, static_cast<int32>(NumOfBytes));
		}
		else if (bSuccess == FALSE && Overlapped != nullptr)
		{
			// GQCS 실패 + Overlapped 유효: I/O 에러 (연결 종료 등)
			// NumOfBytes=0으로 Dispatch 호출하여 상위에서 에러 처리
			IocpEvent* Event = static_cast<IocpEvent*>(Overlapped);
			IocpObject* Object = reinterpret_cast<IocpObject*>(Key);
			Object->Dispatch(Event, 0);
		}
		// else: bSuccess == FALSE && Overlapped == nullptr → GQCS 자체 실패 (타임아웃 등), 무시하고 계속

		return true;
	}
}