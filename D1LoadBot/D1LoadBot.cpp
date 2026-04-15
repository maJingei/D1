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

using namespace D1;
using namespace D1LoadBot;

namespace
{
	struct FConfig
	{
		uint32 Sessions = 100;
		uint32 MoveIntervalMs = 200;
		uint32 DurationSec = 60;
		std::string Host = "127.0.0.1";
		uint16 Port = 9999;
		std::string CsvPath = "loadbot_report.csv";
	};

	// 워커 스레드 개수 = HW concurrency 의 2 배(최소 2, 최대 8). 봇은 1000개 세션까지 스케일해야 하므로 워커 복수 필요.
	static constexpr uint32 kMinWorkers = 2;
	static constexpr uint32 kMaxWorkers = 8;

	// 펌프 틱 주기 — 이동 주기가 이보다 작아도 send 타이밍 오차는 최대 kPumpTickMs.
	static constexpr uint32 kPumpTickMs = 10;

	// 수집 주기 — Metrics 에 샘플을 병합하는 주기.
	static constexpr uint32 kDrainTickMs = 100;

	/** argv 파서. 알려진 옵션만 취급, 미지정 인자는 경고 후 무시. */
	bool ParseArgs(int argc, char* argv[], FConfig& OutConfig)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::string Arg = argv[i];
			auto NextArg = [&](const char* Name) -> const char*
			{
				if (i + 1 >= argc)
				{
					std::printf("[D1LoadBot] Missing value for %s\n", Name);
					return nullptr;
				}
				return argv[++i];
			};

			if (Arg == "--sessions")
			{
				const char* V = NextArg("--sessions");
				if (V == nullptr) return false;
				OutConfig.Sessions = static_cast<uint32>(std::strtoul(V, nullptr, 10));
			}
			else if (Arg == "--move-interval-ms")
			{
				const char* V = NextArg("--move-interval-ms");
				if (V == nullptr) return false;
				OutConfig.MoveIntervalMs = static_cast<uint32>(std::strtoul(V, nullptr, 10));
			}
			else if (Arg == "--duration-sec")
			{
				const char* V = NextArg("--duration-sec");
				if (V == nullptr) return false;
				OutConfig.DurationSec = static_cast<uint32>(std::strtoul(V, nullptr, 10));
			}
			else if (Arg == "--host")
			{
				const char* V = NextArg("--host");
				if (V == nullptr) return false;
				OutConfig.Host = V;
			}
			else if (Arg == "--port")
			{
				const char* V = NextArg("--port");
				if (V == nullptr) return false;
				OutConfig.Port = static_cast<uint16>(std::strtoul(V, nullptr, 10));
			}
			else if (Arg == "--csv")
			{
				const char* V = NextArg("--csv");
				if (V == nullptr) return false;
				OutConfig.CsvPath = V;
			}
			else if (Arg == "--help" || Arg == "-h")
			{
				std::printf("Usage: D1LoadBot [--sessions N] [--move-interval-ms N] [--duration-sec N] [--host IP] [--port P] [--csv PATH]\n");
				return false;
			}
			else
			{
				std::printf("[D1LoadBot] Unknown argument: %s\n", Arg.c_str());
			}
		}
		return true;
	}

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

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	FConfig Config;
	if (ParseArgs(argc, argv, Config) == false)
		return 0;

	std::printf("[D1LoadBot] Start sessions=%u interval=%ums duration=%us target=%s:%u csv=%s\n",
		Config.Sessions, Config.MoveIntervalMs, Config.DurationSec,
		Config.Host.c_str(), Config.Port, Config.CsvPath.c_str());

	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);
	SocketUtils::Init();

	// Service 생성 — BotSession 팩토리 + 이동 주기 전달.
	ClientServiceRef Service = std::make_shared<ClientService>();
	const uint32 MoveIntervalLocal = Config.MoveIntervalMs;
	Service->SetSessionFactory([MoveIntervalLocal]() -> SessionRef
	{
		auto Bot = std::make_shared<BotSession>();
		Bot->SetMoveInterval(MoveIntervalLocal);
		return Bot;
	});

	if (Service->Start() == false)
	{
		std::printf("[D1LoadBot] Service start failed\n");
		SocketUtils::Cleanup();
		PoolManager::GetInstance().Shutdown();
		return 1;
	}

	// 워커 스레드. IOCP dispatch 루프.
	ThreadManager& Manager = ThreadManager::GetInstance();
	const uint32 WorkerCount = DecideWorkerCount();
	for (uint32 i = 0; i < WorkerCount; ++i)
	{
		Manager.CreateThread([Service]()
		{
			while (Service->GetIocpCore()->Dispatch())
			{
			}
		});
	}
	Manager.Launch();

	// Session 생성/연결.
	NetAddress Address(Config.Host, Config.Port);
	std::vector<std::shared_ptr<BotSession>> Bots;
	Bots.reserve(Config.Sessions);
	for (uint32 i = 0; i < Config.Sessions; ++i)
	{
		SessionRef Created = Service->Connect(Address);
		if (Created == nullptr)
		{
			std::printf("[D1LoadBot] Connect failed at index=%u\n", i);
			continue;
		}
		auto Bot = std::static_pointer_cast<BotSession>(Created);
		Bots.push_back(Bot);
	}
	std::printf("[D1LoadBot] Connect requested %zu sessions\n", Bots.size());

	// 측정 초기화.
	Metrics Collect;
	Collect.Start();

	// 펌프 + 드레인 루프. 메인 스레드가 tick 타이머 역할을 한다.
	std::atomic<bool> bStop{ false };
	std::thread PumpThread([&bStop, &Bots]()
	{
		ThreadManager::InitTLS();
		while (bStop.load(std::memory_order_relaxed) == false)
		{
			for (auto& Bot : Bots)
			{
				if (Bot) Bot->PumpMove();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(kPumpTickMs));
		}
		// 펌프 스레드 종료 전 TLS SendBuffer chunk 반환. (없으면 프로세스 종료 시 UAF 가능성)
		SendBufferManager::ShutdownThread();
	});

	const auto StartedAt = std::chrono::steady_clock::now();
	while (true)
	{
		const auto Now = std::chrono::steady_clock::now();
		const auto Elapsed = std::chrono::duration_cast<std::chrono::seconds>(Now - StartedAt).count();
		if (Elapsed >= static_cast<int64>(Config.DurationSec))
			break;

		// 샘플 병합.
		for (auto& Bot : Bots)
		{
			if (Bot == nullptr) continue;
			std::vector<FLatencySample> Samples = Bot->DrainSamples();
			if (Samples.empty() == false)
				Collect.Merge(Samples);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kDrainTickMs));
	}

	// 펌프 정지 + 최종 드레인.
	bStop.store(true, std::memory_order_relaxed);
	if (PumpThread.joinable()) PumpThread.join();

	for (auto& Bot : Bots)
	{
		if (Bot == nullptr) continue;
		Bot->RequestShutdown();
		std::vector<FLatencySample> Samples = Bot->DrainSamples();
		if (Samples.empty() == false)
			Collect.Merge(Samples);
	}

	Collect.Report(Config.CsvPath, Config.DurationSec, Config.Sessions, Config.MoveIntervalMs);

	// 종료 시퀀스 — D1Server 와 동일한 순서 (Service.Stop → 워커 종료 신호 → Join → Service 해제 → Pool Shutdown).
	std::printf("[D1LoadBot] Shutting down...\n");
	Service->Stop();

	for (uint32 i = 0; i < WorkerCount; ++i)
		::PostQueuedCompletionStatus(Service->GetIocpCore()->GetHandle(), 0, 0, nullptr);
	Manager.JoinAll();

	Bots.clear();
	Service.reset();

	SocketUtils::Cleanup();
	SendBufferManager::ShutdownThread();
	PoolManager::GetInstance().Shutdown();
	Manager.DestroyAllThreads();

	std::printf("[D1LoadBot] Done\n");
	return 0;
}