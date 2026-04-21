#include "Core/CoreMinimal.h"
#include "Iocp/SocketUtils.h"
#include "Iocp/NetAddress.h"
#include "Iocp/SendBuffer.h"
#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Job/GlobalJobQueue.h"
#include "Job/JobQueue.h"
#include "ServerEngine.h"
#include "Network/ServerService.h"
#include "Network/GameServerSession.h"
#include "DB/DBJobQueue.h"
#include <iostream>
#include <chrono>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// ── Step 1: Engine Init ─────────────────────────────────────
	ServerEngine Engine;
	if (Engine.Init() == false)
	{
		std::cout << "[Server] ServerEngine init failed!\n";
		return 1;
	}

	// ── Step 2: ServerService 생성 및 시작 ──────────────────────
	ServerServiceRef Server = std::make_shared<ServerService>(NetAddress::AnyAddress(9999),
		[]() -> SessionRef
		{
			return std::make_shared<GameServerSession>();
		});

	if (Server->Start() == false)
	{
		std::cout << "[Server] ServerService start failed!\n";
		SocketUtils::Cleanup();
		return 1;
	}

	// ── Step 3: Engine BeginPlay (TimerLoop 시작) ───────────────
	Engine.BeginPlay();

	std::cout << "========================================\n";
	std::cout << "  D1Server (port 9999)\n";
	std::cout << "  Press Enter to shutdown...\n";
	std::cout << "========================================\n";

	// ── Step 4: 워커 스레드 생성 ────────────────────────────────
	static constexpr int32 IOCP_WORKER_COUNT = 5;
	static constexpr int32 FLUSH_WORKER_COUNT = 5;

	ThreadManager& Manager = ThreadManager::GetInstance();

	// IOCP 워커
	for (int32 i = 0; i < IOCP_WORKER_COUNT; i++)
	{
		Manager.CreateThread([Server]()
		{
			while (Server->GetIocpCore()->Dispatch())
			{
			}
			std::cout << "[Worker] Dispatch loop exited\n";
		});
	}

	// Flush Worker 종료 신호
	std::atomic<bool> bFlushWorkerRun{ true };

	// Flush Worker: GlobalJobQueue 에서 JobQueue 를 꺼내 FlushJob 을 호출한다.
	for (int32 i = 0; i < FLUSH_WORKER_COUNT; i++)
	{
		Manager.CreateThread([&bFlushWorkerRun]()
		{
			while (true)
			{
				JobQueueRef Queue = GlobalJobQueue::GetInstance().Pop(bFlushWorkerRun);
				if (Queue == nullptr)
					break;
				Queue->FlushJob();
			}
			std::cout << "[FlushWorker] exited\n";
		});
	}

	// DB Worker 종료 신호 — v1 은 워커 1 개(Round 6 결정).
	std::atomic<bool> bDBWorkerRun{ true };
	static constexpr int32 DB_WORKER_COUNT = 1;

	// DB Worker: 전용 스레드가 DBJobQueue 를 10ms 간격으로 Drain. condition_variable 대신
	// 폴링을 택한 이유는 v1 최소형 + DB 호출 자체가 수~수십 ms 범위라 10ms 폴링 지연이 무시 가능.
	for (int32 i = 0; i < DB_WORKER_COUNT; i++)
	{
		Manager.CreateThread([&bDBWorkerRun]()
		{
			while (bDBWorkerRun.load(std::memory_order_relaxed))
			{
				DBJobQueue::GetInstance().Drain();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			std::cout << "[DBWorker] exited\n";
		});
	}

	Manager.Launch();

	// ── Step 5: 메인 Tick 루프 — Enter 키 감지 시 복귀한다. ─────
	Engine.Tick();

	// ── Step 6: 종료 시퀀스 ─────────────────────────────────────
	std::cout << "[Server] Shutting down...\n";
	
	// (1) ServerService 정지
	Server->Stop();
	std::cout << "[IOCP quit signal]\n";

	// (2) IOCP 워커 종료 신호 — 워커 1개당 PQCS 1건이 dequeue 되어야 Dispatch 루프가 종료된다.
	//     IOCP_WORKER_COUNT 만큼 게시하지 않으면 일부 워커가 GetQueuedCompletionStatus(INFINITE)에서 영원히 대기한다.
	for (int32 i = 0; i < IOCP_WORKER_COUNT; ++i)
		::PostQueuedCompletionStatus(Server->GetIocpCore()->GetHandle(), 0, 0, nullptr);

	// (3) Flush Worker 종료
	bFlushWorkerRun.store(false, std::memory_order_relaxed);
	GlobalJobQueue::GetInstance().WakeAll();

	// (3b) DB Worker 종료 — 폴링 루프이므로 별도 Wake 없이 플래그만 내리면 다음 sleep 해제 후 종료.
	bDBWorkerRun.store(false, std::memory_order_relaxed);

	// (4) 모든 워커 Join
	Manager.JoinAll();
	std::cout << "[Worker joined]\n";

	// (5) Server shared_ptr 해제 — ~Service 의 Sessions(Set<SessionRef>) sentinel 노드를
	//     deallocate 하려면 PoolManager 가 살아 있어야 한다. 따라서 Engine.Destroy 의
	//     PoolManager::Shutdown() 보다 *반드시 먼저* 호출해야 한다.
	Server.reset();
	std::cout << "[Server released]\n";

	// (6) 메인 스레드 TLS SendBufferChunk 참조 해제.
	SendBufferManager::ShutdownThread();

	// (7) Engine Destroy (TimerLoop 정지 → World.Destroy → Level.Destroy → PoolManager.Shutdown → ThreadManager 정리)
	Engine.Destroy();

	// (8) Winsock 정리
	SocketUtils::Cleanup();

	std::cout << "[Server] Shutdown complete\n";
	return 0;
}