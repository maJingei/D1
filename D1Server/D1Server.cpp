#include <iostream>
#include <memory>

#include "Iocp/SocketUtils.h"
#include "Iocp/NetAddress.h"
#include "ServerService.h"
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

	// Step 2: ServerService 생성 및 시작
	std::shared_ptr<ServerService> Server = std::make_shared<ServerService>();
	Server->SetSessionFactory([]() -> std::shared_ptr<Session> { return std::make_shared<Session>(); });

	NetAddress Address = NetAddress::AnyAddress(9999);

	if (Server->Start(Address) == false)
	{
		std::cout << "[Server] ServerService start failed!" << std::endl;
		SocketUtils::Cleanup();
		return 1;
	}

	std::cout << "========================================" << std::endl;
	std::cout << "  D1Server Echo Server (port 9999)" << std::endl;
	std::cout << "  Press Enter to shutdown..." << std::endl;
	std::cout << "========================================" << std::endl;

	// Step 3: 워커 스레드 1개 생성
	ThreadManager& Manager = ThreadManager::GetInstance();
	Manager.CreateThread([Server]()
	{
		while (Server->GetIocpCore()->Dispatch())
		{
		}
		std::cout << "[Worker] Dispatch loop exited" << std::endl;
	});
	Manager.Launch();

	// Step 4: Enter 키로 종료 대기
	std::cin.get();

	// Step 5: 종료 시퀀스
	std::cout << "[Server] Shutting down..." << std::endl;

	// 워커 스레드에 종료 신호 전송
	::PostQueuedCompletionStatus(Server->GetIocpCore()->GetHandle(), 0, 0, nullptr);
	Manager.JoinAll();

	// 세션 정리
	Server->Stop();

	// Winsock 정리
	SocketUtils::Cleanup();

	// 메모리 풀 정리
	PoolManager::GetInstance().Shutdown();

	Manager.DestroyAllThreads();

	std::cout << "[Server] Shutdown complete" << std::endl;

	return 0;
}
