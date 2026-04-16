#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "BotSession.h"
#include "Metrics.h"

#include "Iocp/IocpCore.h"
#include "Iocp/NetAddress.h"
#include "Iocp/SendBuffer.h"
#include "Iocp/SocketUtils.h"
#include "Memory/MemoryPool.h"
#include "Network/ClientService.h"
#include "Threading/ThreadManager.h"
#include "Core/DiagCounters.h"
#include "Job/GlobalJobQueue.h"
#include "Job/JobQueue.h"

using namespace D1LoadBot;

namespace
{
	/**
	 * 부하 테스트 실행 파라미터.
	 *
	 * 하드코딩 기본값을 inline 초기화로 유지한다.
	 * Args 파싱 없이 소스 수정만으로 값을 바꾸는 구조.
	 */
	struct FBotSettings
	{
		/** 동시 접속 봇 세션 수. */
		uint32 SessionCount = 500;

		/** C_MOVE 전송 주기 (밀리초). */
		uint32 MoveIntervalMs = 200;

		/** 부하 테스트 총 실행 시간 (초). */
		uint32 TestDurationSec = 60;

		/** 대상 서버 IP 또는 호스트명. */
		std::string ServerHost = "127.0.0.1";

		/** 대상 서버 포트. */
		uint16 ServerPort = 9999;

		/** 결과 CSV 출력 경로. */
		std::string ReportCsvPath = "loadbot_report.csv";
	};

	// 워커 스레드 개수 = HW concurrency 의 2 배(최소 2, 최대 8). 봇은 1000개 세션까지 스케일해야 하므로 워커 복수 필요.
	static constexpr uint32 kMinWorkers = 2;
	static constexpr uint32 kMaxWorkers = 8;

	// 펌프 틱 주기 — 이동 주기가 이보다 작아도 send 타이밍 오차는 최대 kPumpTickMs.
	static constexpr uint32 kPumpTickMs = 10;

	// 수집 주기 — Metrics 에 샘플을 병합하는 주기.
	static constexpr uint32 kDrainTickMs = 100;

	/**
	 * 적절한 IOCP 워커 스레드 개수를 결정한다.
	 *
	 * HW concurrency 의 2 배를 기준으로 [kMinWorkers, kMaxWorkers] 범위로 클램핑한다.
	 */
	uint32 DecideWorkerCount()
	{
		const uint32 HwRaw = std::thread::hardware_concurrency();
		const uint32 Hw = (HwRaw == 0) ? 1 : HwRaw;
		const uint32 Raw = Hw * 2;
		if (Raw < kMinWorkers) return kMinWorkers;
		if (Raw > kMaxWorkers) return kMaxWorkers;
		return Raw;
	}
}

int main()
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 실행 파라미터 — 기본값 그대로 사용. 값 변경은 FBotSettings 멤버를 직접 수정.
	const FBotSettings Settings;

	std::printf("[D1LoadBot] Start sessions=%u interval=%ums duration=%us target=%s:%u csv=%s\n",
		Settings.SessionCount, Settings.MoveIntervalMs, Settings.TestDurationSec,
		Settings.ServerHost.c_str(), Settings.ServerPort, Settings.ReportCsvPath.c_str());

	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);
	SocketUtils::Init();

	// BotService 생성 — BotSession 팩토리에 이동 주기를 캡처해 전달한다.
	const uint32 MoveIntervalCaptured = Settings.MoveIntervalMs;
	ClientServiceRef BotClientService = std::make_shared<ClientService>(
		NetAddress(Settings.ServerHost, Settings.ServerPort),
		[MoveIntervalCaptured]() -> SessionRef
		{
			auto Bot = std::make_shared<BotSession>();
			Bot->SetMoveInterval(MoveIntervalCaptured);
			return Bot;
		});

	if (BotClientService->Start() == false)
	{
		std::printf("[D1LoadBot] Service start failed\n");
		SocketUtils::Cleanup();
		PoolManager::GetInstance().Shutdown();
		return 1;
	}

	// IOCP dispatch 워커 스레드 풀 생성.
	ThreadManager& ThreadMgr = ThreadManager::GetInstance();
	const uint32 WorkerCount = DecideWorkerCount();
	for (uint32 i = 0; i < WorkerCount; ++i)
	{
		ThreadMgr.CreateThread([BotClientService]()
		{
			while (BotClientService->GetIocpCore()->Dispatch())
			{
			}
		});
	}

	// Flush Worker — BotSession::Send 가 Serializer Job 으로 Push 되므로 소비 주체가 필요하다.
	// 없으면 WSASend 가 영원히 발사되지 않아 서버에 아무 패킷도 도달하지 않는다.
	std::atomic<bool> bFlushWorkerRunning{ true };
	for (int32 i = 0; i < FLUSH_WORKER_COUNT; ++i)
	{
		ThreadMgr.CreateThread([&bFlushWorkerRunning]()
		{
			while (true)
			{
				JobQueueRef Queue = GlobalJobQueue::GetInstance().Pop(bFlushWorkerRunning);
				if (Queue == nullptr)
					break;
				Queue->FlushJob();
			}
		});
	}

	ThreadMgr.Launch();

	// 봇 세션 생성 및 연결 요청.
	std::vector<std::shared_ptr<BotSession>> BotSessions;
	BotSessions.reserve(Settings.SessionCount);
	for (uint32 i = 0; i < Settings.SessionCount; ++i)
	{
		SessionRef Created = BotClientService->Connect();
		if (Created == nullptr)
		{
			std::printf("[D1LoadBot] Connect failed at index=%u\n", i);
			continue;
		}
		auto Bot = std::static_pointer_cast<BotSession>(Created);
		BotSessions.push_back(Bot);
	}
	std::printf("[D1LoadBot] Connect requested %zu sessions\n", BotSessions.size());

	// 메트릭 수집기 초기화.
	Metrics MetricsAggregator;
	MetricsAggregator.Start();

	// 펌프 스레드 — 매 kPumpTickMs 마다 각 봇의 PumpMove 를 호출해 이동 타이머를 구동한다.
	std::atomic<bool> bPumpShouldStop{ false };
	std::thread MoveScheduleThread([&bPumpShouldStop, &BotSessions]()
	{
		ThreadManager::InitTLS();
		while (bPumpShouldStop.load(std::memory_order_relaxed) == false)
		{
			for (auto& Bot : BotSessions)
			{
				if (Bot) Bot->PumpMove();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(kPumpTickMs));
		}
		// 펌프 스레드 종료 전 TLS SendBuffer chunk 반환. (없으면 프로세스 종료 시 UAF 가능성)
		SendBufferManager::ShutdownThread();
	});

	// 메인 드레인 루프 — TestDurationSec 동안 샘플·카운터를 kDrainTickMs 주기로 병합한다.
	const auto StartedAt = std::chrono::steady_clock::now();
	while (true)
	{
		const auto Now = std::chrono::steady_clock::now();
		const auto Elapsed = std::chrono::duration_cast<std::chrono::seconds>(Now - StartedAt).count();
		if (Elapsed >= static_cast<int64>(Settings.TestDurationSec))
			break;

		// 샘플 + drop/timeout 카운터 병합.
		for (auto& Bot : BotSessions)
		{
			if (Bot == nullptr) continue;
			std::vector<FLatencySample> Samples = Bot->DrainSamples();
			if (Samples.empty() == false)
				MetricsAggregator.Merge(Samples);
			MetricsAggregator.MergeDropCount(Bot->DrainDropCount());
			MetricsAggregator.MergeTimeoutCount(Bot->DrainTimeoutCount());
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kDrainTickMs));
	}

	// 펌프 정지 후 최종 드레인 — 테스트 종료 직전 잔여 샘플을 모두 수집한다.
	bPumpShouldStop.store(true, std::memory_order_relaxed);
	if (MoveScheduleThread.joinable()) MoveScheduleThread.join();

	for (auto& Bot : BotSessions)
	{
		if (Bot == nullptr) continue;
		Bot->RequestShutdown();
		// SendMove와 Move패킷 수신으로 저장된 샘플 배열을 가져와 테스트 분석기에 합칩니다.
		std::vector<FLatencySample> Samples = Bot->DrainSamples();
		if (Samples.empty() == false)
			MetricsAggregator.Merge(Samples);
		MetricsAggregator.MergeDropCount(Bot->DrainDropCount());
		MetricsAggregator.MergeTimeoutCount(Bot->DrainTimeoutCount());
	}

	MetricsAggregator.Report(Settings.ReportCsvPath, Settings.TestDurationSec, Settings.SessionCount, Settings.MoveIntervalMs);

	// 종료 시퀀스 — D1Server 와 동일한 순서 (Service.Stop → 워커 종료 신호 → Join → Service 해제 → Pool Shutdown).
	std::printf("[D1LoadBot] Shutting down...\n");
	BotClientService->Stop();

	for (uint32 i = 0; i < WorkerCount; ++i)
		::PostQueuedCompletionStatus(BotClientService->GetIocpCore()->GetHandle(), 0, 0, nullptr);

	// Flush Worker 종료: Pop 대기 중인 워커를 깨워 nullptr 을 받게 만든다.
	bFlushWorkerRunning.store(false, std::memory_order_relaxed);
	GlobalJobQueue::GetInstance().WakeAll();

	ThreadMgr.JoinAll();

	BotSessions.clear();
	BotClientService.reset();

	SocketUtils::Cleanup();
	SendBufferManager::ShutdownThread();
	PoolManager::GetInstance().Shutdown();
	ThreadMgr.DestroyAllThreads();

	std::printf("[D1LoadBot] Done\n");
	return 0;
}