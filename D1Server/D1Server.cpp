#include <iostream>

#include "Iocp/SocketUtils.h"
#include "Iocp/IocpCore.h"
#include "Iocp/Listener.h"
#include "Iocp/Session.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"

using namespace D1;

int main(int argc, char* argv[])
{
	// 메인 스레드 TLS 초기화 (ID=1)
	ThreadManager::InitTLS();

	// PoolManager 초기화: 14개 크기 클래스 풀 생성, 청크당 64블록
	PoolManager::GetInstance().Initialize(64);

	// Step 1: Winsock 초기화 + LPFN 바인딩
	SocketUtils::Init();

	// Step 2: IOCP 핸들 생성
	IocpCore Core;
	Core.Initialize();

	// Step 3: Listener 시작 (Bind/Listen/Register/PostAccept)
	Listener ServerListener;
	SOCKADDR_IN Address = {};
	Address.sin_family = AF_INET;
	Address.sin_addr.s_addr = ::htonl(INADDR_ANY);
	Address.sin_port = ::htons(9999);

	if (ServerListener.Start(Address, &Core) == false)
	{
		std::cout << "[Server] Listener start failed!" << std::endl;
		SocketUtils::Cleanup();
		return 1;
	}

	std::cout << "========================================" << std::endl;
	std::cout << "  D1Server Echo Server (port 9999)" << std::endl;
	std::cout << "  Press Enter to shutdown..." << std::endl;
	std::cout << "========================================" << std::endl;

	// Step 4: 워커 스레드 1개 생성
	ThreadManager& Manager = ThreadManager::GetInstance();
	Manager.CreateThread([&Core]()
	{
		while (Core.Dispatch())
		{
		}
		std::cout << "[Worker] Dispatch loop exited" << std::endl;
	});
	Manager.Launch();

	// Step 5: Enter 키로 종료 대기
	std::cin.get();

	// Step 6: 종료 시퀀스
	std::cout << "[Server] Shutting down..." << std::endl;

	// 워커 스레드에 종료 신호 전송
	::PostQueuedCompletionStatus(Core.GetHandle(), 0, 0, nullptr);
	Manager.JoinAll();

	// 세션 정리
	ServerListener.CloseAllSessions();

	// Listener 소멸자에서 ListenSocket 정리
	// IocpCore 소멸자에서 IOCP 핸들 정리

	// Winsock 정리
	SocketUtils::Cleanup();

	// 메모리 풀 정리
	PoolManager::GetInstance().Shutdown();

	Manager.DestroyAllThreads();

	std::cout << "[Server] Shutdown complete" << std::endl;

	return 0;
}