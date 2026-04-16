#include "IocpCore.h"
#include "IocpObject.h"
#include "IocpEvent.h"
#include <cassert>

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

	// 반환값 계약:
	//   true  = 이번 호출에서 실제 이벤트(정상/에러) 하나를 처리했다
	//   false = 처리할 이벤트가 없었다 (종료 신호 수신 또는 GQCS 타임아웃)
	// 이 계약은 non-blocking 드레인(while(Dispatch(0)) {})과
	// blocking 워커 루프(while(Dispatch()) {} with INFINITE)를 모두 만족시킨다.

	// 종료 신호: PQCS(0, 0, nullptr)가 성공적으로 dequeue된 경우
	if (bSuccess == TRUE && Key == 0 && Overlapped == nullptr)
	{
		return false;
	}

	// Key는 non-authoritative. Event->Owner가 수명 권위 소스.
	// Key는 향후 per-object ID / GetQueuedCompletionStatusEx 배치 경로용으로 시그니처만 보존.

	if (bSuccess == TRUE && Overlapped != nullptr)
	{
		// 성공 경로: OVERLAPPED → IocpEvent.
		// Event->Owner를 move로 KeepAlive 스택 로컬로 옮겨 Dispatch 실행 구간 수명 보장.
		// self-cycle(Session→Event→shared_ptr<Session>)은 이 move로 해제된다.
		IocpEvent* Event = static_cast<IocpEvent*>(Overlapped);
		IocpObjectRef KeepAlive = std::move(Event->Owner);
#ifdef _DEBUG
		assert(KeepAlive); // HoldForIo 규약 위반 또는 Owner.reset 오남용 감지
#endif
		if (!KeepAlive)
		{
			// Release 안전망: late/orphan completion은 조용히 흡수한다.
			return true;
		}
		KeepAlive->Dispatch(Event, static_cast<int32>(NumOfBytes));
		return true;
	}

	if (bSuccess == FALSE && Overlapped != nullptr)
	{
		// 에러 경로: I/O 에러(연결 종료 등). 동일한 KeepAlive 패턴 적용.
		// NumOfBytes=0으로 Dispatch 호출하여 상위에서 에러 처리.
		IocpEvent* Event = static_cast<IocpEvent*>(Overlapped);
		IocpObjectRef KeepAlive = std::move(Event->Owner);
#ifdef _DEBUG
		assert(KeepAlive);
#endif
		if (!KeepAlive)
		{
			return true;
		}
		KeepAlive->Dispatch(Event, 0);
		return true;
	}

	// bSuccess == FALSE && Overlapped == nullptr
	// → GQCS 자체 실패 (대표적으로 WAIT_TIMEOUT). 이번 호출에서 처리한 이벤트 없음.
	// non-blocking 드레인 호출자는 이걸 받아 루프를 빠져나가야 UI 메시지 루프 등으로 되돌아갈 수 있다.
	return false;
}
