#include <iostream>
#include <memory>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "Iocp/SocketUtils.h"
#include "Iocp/NetAddress.h"
#include "Iocp/SendBuffer.h"
#include "ServerService.h"
#include "ClientPacketHandler.h"
#include "Iocp/Session.h"
#include "Iocp/PacketSession.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"

using namespace D1;

namespace
{
	/**
	 * 서버 측 세션: PacketSession 을 상속하여 OnRecvPacket 을 서버 핸들러 테이블로 디스패치한다.
	 */
	class GameServerSession : public PacketSession
	{
	protected:
		void OnRecvPacket(BYTE* Buffer, int32 Len) override
		{
			PacketSessionRef Ref = GetPacketSessionRef();
			ClientPacketHandler::HandlePacket(Ref, Buffer, Len);
		}
	};
}

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	// Debug 빌드 누수 체크: 프로세스 종료 시 CRT가 자동으로 미반환 힙을 리포트한다.
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 메인 스레드 TLS 초기화 (ID=1)
	ThreadManager::InitTLS();

	// PoolManager 초기화: 14개 크기 클래스 풀 생성, 청크당 64블록
	PoolManager::GetInstance().Initialize(64);

	// Step 1: Winsock 초기화 + LPFN 바인딩
	SocketUtils::Init();

	// Step 1.5: 서버 패킷 핸들러 테이블 초기화
	ClientPacketHandler::Init();

	// Step 2: ServerService 생성 및 시작
	ServerServiceRef Server = std::make_shared<ServerService>();
	Server->SetSessionFactory([]() -> SessionRef { return std::make_shared<GameServerSession>(); });

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
	// 불변식(P6): 워커 생존 구간에서만 drain 가능. Stop → PQCS → Join 순서는 역전 금지.
	std::cout << "[Server] Shutting down..." << std::endl;

	// (1) Stop이 내부에서 closesocket → Sessions.clear → ServerListener.reset → drain 루프까지 block.
	Server->Stop();
	std::cout << "[IOCP quit signal]" << std::endl;

	// (2) drain이 끝난 후에야 워커에 종료 신호 전송.
	::PostQueuedCompletionStatus(Server->GetIocpCore()->GetHandle(), 0, 0, nullptr);
	Manager.JoinAll();
	std::cout << "[Worker joined]" << std::endl;

	// (3) Server shared_ptr을 PoolManager::Shutdown 이전에 완전 해제한다.
	// Service의 Sessions(StlAllocator 기반 Set)가 소멸할 때 PoolManager::GetPool을
	// 호출하므로, PoolManager 파괴 후에 Server가 소멸되면 UAF가 발생한다.
	Server.reset();
	std::cout << "[Server released]" << std::endl;

	// Winsock 정리
	SocketUtils::Cleanup();

	// 메인 스레드의 SendBufferManager TLS Chunk를 명시적으로 반환한다.
	// (CRT TLS 소멸자는 PoolManager::Shutdown 이후에 실행되므로 여기서 미리 비운다.)
	SendBufferManager::ShutdownThread();

	// 메모리 풀 정리
	PoolManager::GetInstance().Shutdown();

	Manager.DestroyAllThreads();

	std::cout << "[Server] Shutdown complete" << std::endl;

	return 0;
}
