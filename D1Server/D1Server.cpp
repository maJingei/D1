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
#include "MoveCounter.h"
#include "GameServerSession.h"
#include "GameRoom.h"
#include "Iocp/Session.h"
#include "Iocp/PacketSession.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <windows.h>

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Job/GlobalJobQueue.h"
#include "Job/JobSerializer.h"

using namespace D1;

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

	// Step 1.6: GameRoom 초기화 — 서버 권위적 이동 검증용 Collision CSV 로드.
	// exe 기준 상대 경로: exe 는 Binary\$(Configuration)\ 에 위치하므로 "..\..\Resource\Collision_Collision.csv" 가 SolutionDir\Resource\... 를 가리킨다.
	{
		char ExePath[MAX_PATH] = { 0 };
		::GetModuleFileNameA(nullptr, ExePath, MAX_PATH);
		std::string Path(ExePath);
		const size_t LastSep = Path.find_last_of("\\/");
		const std::string BaseDir = (LastSep != std::string::npos) ? Path.substr(0, LastSep + 1) : std::string();
		const std::string CollisionCsvPath = BaseDir + "..\\..\\Resource\\Collision_Collision.csv";
		if (GameRoom::Get()->Initialize(CollisionCsvPath) == false)
			std::cout << "[Server] GameRoom CollisionMap load failed: " << CollisionCsvPath << std::endl;
	}

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

	// IOCP 워커 및 Flush Worker 개수 상수
	static constexpr int32 IOCP_WORKER_COUNT = 5;
	static constexpr int32 FLUSH_WORKER_COUNT = 3;

	// Step 3: 워커 스레드 생성
	ThreadManager& Manager = ThreadManager::GetInstance();

	// IOCP 워커: 패킷 수신 처리 및 PushJob 담당.
	for (int32 i = 0; i < IOCP_WORKER_COUNT; i++)
	{
		Manager.CreateThread([Server]()
		{
			while (Server->GetIocpCore()->Dispatch())
			{
			}
			std::cout << "[Worker] Dispatch loop exited" << "\n";
		});
	}

	// Flush Worker 종료 신호 — 종료 시 false 로 바꾸면 Pop 대기가 풀린다.
	std::atomic<bool> bFlushWorkerRun{ true };

	// Flush Worker: GlobalJobQueue 에서 JobSerializer 를 꺼내 FlushJob 을 호출한다.
	// Push/Flush 분리 모델 — IOCP 워커는 Push 만, Flush Worker 는 Flush 만 담당.
	for (int32 i = 0; i < FLUSH_WORKER_COUNT; i++)
	{
		Manager.CreateThread([&bFlushWorkerRun]()
		{
			while (true)
			{
				JobSerializerRef Serializer = GlobalJobQueue::GetInstance().Pop(bFlushWorkerRun);
				if (Serializer == nullptr)
					break;
				Serializer->FlushJob();
			}
			std::cout << "[FlushWorker] exited" << "\n";
		});
	}

	Manager.Launch();

	// Step 3.5: TPS 로거. 1초 주기로 GMoveCounter diff 를 찍는다.
	// std::thread 로 별도 관리(ThreadManager 는 TLS 설계상 워커 전용) — bTpsLoggerRun 으로 명시 종료.
	std::atomic<bool> bTpsLoggerRun{ true };
	std::thread TpsLoggerThread([&bTpsLoggerRun]()
	{
		uint64 Prev = GMoveCounter.load(std::memory_order_relaxed);
		auto Last = std::chrono::steady_clock::now();
		while (bTpsLoggerRun.load(std::memory_order_relaxed))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			const uint64 Cur = GMoveCounter.load(std::memory_order_relaxed);
			const uint64 Delta = Cur - Prev;
			Prev = Cur;

			const auto Now = std::chrono::steady_clock::now();
			const double ElapsedSec = std::chrono::duration<double>(Now - Last).count();
			Last = Now;

			const double Tps = (ElapsedSec > 0.0) ? (static_cast<double>(Delta) / ElapsedSec) : 0.0;
			std::cout << "[TPS] moves/sec=" << Tps << " (delta=" << Delta << " total=" << Cur << ")" << std::endl;
		}
	});

	// Step 4: Enter 키로 종료 대기
	std::cin.get();

	// TPS 로거 먼저 종료 — Join 은 Manager 종료 이전에 수행한다.
	bTpsLoggerRun.store(false, std::memory_order_relaxed);
	if (TpsLoggerThread.joinable()) TpsLoggerThread.join();

	// Step 5: 종료 시퀀스
	// 불변식(P6): 워커 생존 구간에서만 drain 가능. Stop → PQCS → Join 순서는 역전 금지.
	std::cout << "[Server] Shutting down..." << std::endl;

	// (1) Stop이 내부에서 closesocket → Sessions.clear → ServerListener.reset → drain 루프까지 block.
	Server->Stop();
	std::cout << "[IOCP quit signal]" << std::endl;

	// (2) drain이 끝난 후에야 워커에 종료 신호 전송.
	::PostQueuedCompletionStatus(Server->GetIocpCore()->GetHandle(), 0, 0, nullptr);

	// Flush Worker 종료: bFlushWorkerRun 을 false 로 바꾸고 모든 대기 Worker 를 깨운다.
	// WakeAll 이후 Pop 에서 nullptr 이 반환되어 Flush Worker 루프가 종료된다.
	bFlushWorkerRun.store(false, std::memory_order_relaxed);
	GlobalJobQueue::GetInstance().WakeAll();

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
