#include "IocpEvent.h"

namespace D1
{
	IocpEvent::IocpEvent(EventType InType)
		: Type(InType)
		, Owner(nullptr)
	{
		Init();
	}

	void IocpEvent::Init()
	{
		// OVERLAPPED 부분만 재초기화. Type, Owner는 유지된다.
		ZeroMemory(static_cast<OVERLAPPED*>(this), sizeof(OVERLAPPED));
	}
}