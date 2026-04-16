#include "Network/ServerService.h"

#include <winsock2.h>
#include <iostream>

bool ServerService::Start()
{
	if (Service::Start() == false)
		return false;

	// Listener는 IocpObject 기반이므로 반드시 make_shared로 생성한다.
	ServerListener = std::make_shared<Listener>();
	if (ServerListener->Start(Address, weak_from_this()) == false)
	{
		return false;
	}
	
	std::cout << "[ServerService] Started (" << Address.GetIp() << ":" << Address.GetPort() << ")" << std::endl;
	return true;
}

void ServerService::Stop()
{
	std::cout << "[Stop begin]" << std::endl;

	// (1) Listener::Shutdown()으로 listen 소켓을 닫아 pending AcceptEx들을 abort 완료로 drain한다.
	//     Shutdown이 ListenSocket을 INVALID_SOCKET으로 즉시 전환하므로, abort 완료가
	//     ProcessAccept로 올라와도 고스트 세션이 생성되지 않는다.
	//     *** 불변식 P6: 이 시점에 Dispatch 워커는 반드시 살아 있어야 한다. ***
	if (ServerListener)
	{
		ServerListener->Shutdown();
	}

	// (2) 모든 Session의 pending WSARecv/WSASend도 에러 완료로 drain.
	//     Sessions.clear()는 각 Session의 마지막 외부 ref를 drop.
	//     self-cycle은 Dispatch 경계에서 해제 중이므로 누수 없음.
	Sessions.clear();

	// (3) Listener 외부 strong ref(ServerListener)를 놓기 전 weak_ptr로 수명 관찰.
	//     아직 pending AcceptEx가 있으면 Event->Owner가 self-ref를 유지 중이므로
	//     ListenerProbe.expired()가 false인 동안은 drain 미완.
	std::weak_ptr<Listener> ListenerProbe = ServerListener;
	ServerListener.reset();

	// (4) drain 루프: 워커 생존 구간에서만 유효.
	//     Sessions + Listener 양측이 모두 비워질 때까지 polling 대기.
	//     타임아웃 3초 초과 시 경고 로그 + 강제 진행 (누수 수용 < hang 회피).
	const DWORD DrainStartMs = ::GetTickCount();
	const DWORD DrainTimeoutMs = 3000;
	while (GetSessionCount() > 0 || ListenerProbe.expired() == false)
	{
		if (::GetTickCount() - DrainStartMs > DrainTimeoutMs)
		{
			std::cerr << "[Stop] drain timeout — workers may have exited prematurely "
					  << "(remaining sessions=" << GetSessionCount()
					  << ", listener alive=" << (ListenerProbe.expired() ? "no" : "yes") << ")" << std::endl;
			break;
		}
		::Sleep(10);
	}

	std::cout << "[Stop drain OK]" << std::endl;
}
