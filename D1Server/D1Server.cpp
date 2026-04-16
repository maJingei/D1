#include "Core/CoreMinimal.h"
#include "Iocp/SocketUtils.h"
#include "Iocp/NetAddress.h"
#include "Iocp/SendBuffer.h"
#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Job/GlobalJobQueue.h"
#include "Job/JobQueue.h"
#include "Core/DiagCounters.h"
#include "ServerEngine.h"
#include "Network/ServerService.h"
#include "MoveCounter.h"
#include "Network/GameServerSession.h"
#include "MetricsCsvWriter.h" // LOG LOGIC
#include <iostream>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

// LOG LOGIC : 측정 모듈 ON/OFF + 출력 경로. 모듈 통째 제거 시 이 세 줄과 관련 호출 모두 grep "LOG LOGIC" 으로 일괄 식별.
// METRICS_ROOM_COUNT 는 World::LEVEL_COUNT 와 동일하게 유지할 것 (헤더 의존성 회피용 상수 미러).
static constexpr bool METRICS_ENABLED = true;
static constexpr const char* METRICS_OUTPUT_PATH = "server_metrics.csv";
static constexpr uint32_t METRICS_ROOM_COUNT = 2;

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	MetricsCsvWriter::Initialize(METRICS_ENABLED, METRICS_OUTPUT_PATH, METRICS_ROOM_COUNT); // LOG LOGIC

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
			MetricsCsvWriter::TryStartTimeline(); // LOG LOGIC : 첫 세션 생성 시각을 bucket 0 으로 CAS
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
		Manager.CreateThread([i, &bFlushWorkerRun]()
		{
			while (true)
			{
				JobQueueRef Queue = GlobalJobQueue::GetInstance().Pop(bFlushWorkerRun);
				if (Queue == nullptr)
					break;
				Queue->FlushJob();
				GWorkerJobCount[i].fetch_add(1, std::memory_order_relaxed);
			}
			std::cout << "[FlushWorker] exited\n";
		});
	}

	// [DIAG] 리포터 스레드
	Manager.CreateThread([&bFlushWorkerRun]()
	{
		uint64 PrevWorker[FLUSH_WORKER_COUNT] = {};
		uint64 PrevBroadcastNs = 0;
		uint64 PrevBroadcastCall = 0;
		uint64 PrevBroadcastSession = 0;
		uint64 PrevSendAppend = 0;
		uint64 PrevSendRegister = 0;
		uint64 PrevBatchSum = 0;
		uint64 PrevBatchCount = 0;
		uint64 PrevRoomTryMove[DIAG_ROOM_COUNT] = {};

		while (bFlushWorkerRun.load(std::memory_order_relaxed))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(DIAG_REPORT_INTERVAL_MS));
			if (bFlushWorkerRun.load(std::memory_order_relaxed) == false)
				break;

			MetricsCsvWriter::Tick(); // LOG LOGIC : 1초 bucket 누적

			uint64 CurWorker[FLUSH_WORKER_COUNT];
			for (int32 Idx = 0; Idx < FLUSH_WORKER_COUNT; Idx++)
				CurWorker[Idx] = GWorkerJobCount[Idx].load(std::memory_order_relaxed);

			const uint64 CurBroadcastNs = GDoBroadcastNsSum.load(std::memory_order_relaxed);
			const uint64 CurBroadcastCall = GDoBroadcastCallCount.load(std::memory_order_relaxed);
			const uint64 CurBroadcastSession = GDoBroadcastSessionSum.load(std::memory_order_relaxed);
			const uint64 CurSendAppend = GSendAppendCount.load(std::memory_order_relaxed);
			const uint64 CurSendRegister = GSendRegisterCount.load(std::memory_order_relaxed);
			const uint64 CurBatchSum = GSendBatchSizeSum.load(std::memory_order_relaxed);
			const uint64 CurBatchCount = GSendBatchSizeCount.load(std::memory_order_relaxed);
			const int32 InflightMax = GInflightWorkerMax.exchange(0, std::memory_order_relaxed);

			uint64 CurRoomTryMove[DIAG_ROOM_COUNT];
			for (int32 Idx = 0; Idx < DIAG_ROOM_COUNT; Idx++)
				CurRoomTryMove[Idx] = GRoomTryMoveCount[Idx].load(std::memory_order_relaxed);

			const uint64 DeltaBroadcastNs = CurBroadcastNs - PrevBroadcastNs;
			const uint64 DeltaBroadcastCall = CurBroadcastCall - PrevBroadcastCall;
			const uint64 DeltaBroadcastSession = CurBroadcastSession - PrevBroadcastSession;
			const uint64 DeltaSendAppend = CurSendAppend - PrevSendAppend;
			const uint64 DeltaSendRegister = CurSendRegister - PrevSendRegister;
			const uint64 DeltaBatchSum = CurBatchSum - PrevBatchSum;
			const uint64 DeltaBatchCount = CurBatchCount - PrevBatchCount;

			const double AvgBroadcastUs = (DeltaBroadcastCall > 0)
				? (static_cast<double>(DeltaBroadcastNs) / static_cast<double>(DeltaBroadcastCall)) / 1000.0
				: 0.0;
			const double AvgBroadcastN = (DeltaBroadcastCall > 0)
				? static_cast<double>(DeltaBroadcastSession) / static_cast<double>(DeltaBroadcastCall)
				: 0.0;
			const double AvgBatchSize = (DeltaBatchCount > 0)
				? static_cast<double>(DeltaBatchSum) / static_cast<double>(DeltaBatchCount)
				: 0.0;

			std::string WorkerStr = "Worker[0.." + std::to_string(FLUSH_WORKER_COUNT - 1) + "]=";
			for (int32 Idx = 0; Idx < FLUSH_WORKER_COUNT; Idx++)
			{
				if (Idx > 0) WorkerStr += "/";
				WorkerStr += std::to_string(CurWorker[Idx] - PrevWorker[Idx]);
			}

			std::cout << "[DIAG] " << WorkerStr
				<< " DoBroadcast avg=" << AvgBroadcastUs << "us"
				<< " calls=" << DeltaBroadcastCall
				<< " avgN=" << AvgBroadcastN
				<< " Send append=" << DeltaSendAppend
				<< " register=" << DeltaSendRegister
				<< " batchAvg=" << AvgBatchSize
				<< " inflightMax=" << InflightMax
				<< "\n";

			// Level 별 TryMove 델타 출력
			std::cout << "[LEVEL] L0=" << (CurRoomTryMove[0] - PrevRoomTryMove[0])
				<< " L1=" << (CurRoomTryMove[1] - PrevRoomTryMove[1])
				<< "\n";

			for (int32 Idx = 0; Idx < FLUSH_WORKER_COUNT; Idx++)
				PrevWorker[Idx] = CurWorker[Idx];
			PrevBroadcastNs = CurBroadcastNs;
			PrevBroadcastCall = CurBroadcastCall;
			PrevBroadcastSession = CurBroadcastSession;
			PrevSendAppend = CurSendAppend;
			PrevSendRegister = CurSendRegister;
			PrevBatchSum = CurBatchSum;
			PrevBatchCount = CurBatchCount;
			for (int32 Idx = 0; Idx < DIAG_ROOM_COUNT; Idx++)
				PrevRoomTryMove[Idx] = CurRoomTryMove[Idx];
		}
	});

	Manager.Launch();

	// TPS 로거
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
			std::cout << "[TPS] moves/sec=" << Tps << " (delta=" << Delta << " total=" << Cur << ")\n";
		}
	});

	// ── Step 5: Enter 키로 종료 대기 ────────────────────────────
	std::cin.get();

	// TPS 로거 종료
	bTpsLoggerRun.store(false, std::memory_order_relaxed);
	if (TpsLoggerThread.joinable()) TpsLoggerThread.join();

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

	// (4) 모든 워커 Join
	Manager.JoinAll();
	std::cout << "[Worker joined]\n";

	// (5) Server shared_ptr 해제 — ~Service 의 Sessions(Set<SessionRef>) sentinel 노드를
	//     deallocate 하려면 PoolManager 가 살아 있어야 한다. 따라서 Engine.Destroy 의
	//     PoolManager::Shutdown() 보다 *반드시 먼저* 호출해야 한다.
	Server.reset();
	std::cout << "[Server released]\n";

	// (6) Engine Destroy (TimerLoop 정지 → World.Destroy → Level.Destroy → PoolManager.Shutdown → ThreadManager 정리)
	Engine.Destroy();

	// (7) Winsock + TLS 정리
	SocketUtils::Cleanup();
	SendBufferManager::ShutdownThread();

	MetricsCsvWriter::Flush(); // LOG LOGIC : server_metrics.csv 출력

	std::cout << "[Server] Shutdown complete\n";
	return 0;
}