#include "IocpEvent.h"

IocpEvent::IocpEvent(EventType InType)
	: Type(InType)
{
	Init();
}

void IocpEvent::Init()
{
	// OVERLAPPED 부분만 재초기화. Type, Owner는 유지된다.
	ZeroMemory(static_cast<OVERLAPPED*>(this), sizeof(OVERLAPPED));
}
