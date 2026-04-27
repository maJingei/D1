#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "BotSession.h"

#include "Iocp/IocpCore.h"
#include "Iocp/NetAddress.h"
#include "Iocp/SendBuffer.h"
#include "Iocp/SocketUtils.h"
#include "Memory/MemoryPool.h"
#include "Network/ClientService.h"
#include "Threading/ThreadManager.h"
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
		uint32 SessionCount = 30;

		/** C_MOVE 전송 주기 (밀리초). */
		uint32 MoveIntervalMs = 200;

		/** 부하 테스트 총 실행 시간 (초). */
		uint32 TestDurationSec = 60;

		/** 대상 서버 IP 또는 호스트명. */
		std::string ServerHost = "127.0.0.1";

		/** 대상 서버 포트. */
		uint16 ServerPort = 9999;
	};

	// 워커 스레드 개수 = HW concurrency 의 2 배(최소 2, 최대 8).
	static constexpr uint32 kMinWorkers = 2;
	static constexpr uint32 kMaxWorkers = 8;

	// 펌프 틱 주기 — 이동 주기가 이보다 작아도 send 타이밍 오차는 최대 kPumpTickMs.
	static constexpr uint32 kPumpTickMs = 10;

	// GlobalJobQueue 를 소비해 WSASend 를 발사하는 Flush 워커 스레드 수.
	static constexpr int32 FLUSH_WORKER_COUNT = 3;

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
			continue;
		auto Bot = std::static_pointer_cast<BotSession>(Created);
		BotSessions.push_back(Bot);
	}

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

	// 메인 대기 루프 — TestDurationSec 동안 펌프 스레드가 이동을 구동한다.
	std::this_thread::sleep_for(std::chrono::seconds(Settings.TestDurationSec));

	// 펌프 정지 후 각 봇 정리.
	bPumpShouldStop.store(true, std::memory_order_relaxed);
	if (MoveScheduleThread.joinable()) MoveScheduleThread.join();

	for (auto& Bot : BotSessions)
	{
		if (Bot == nullptr) continue;
		Bot->RequestShutdown();
	}

	// 종료 시퀀스 — D1Server 와 동일한 순서 (Service.Stop → 워커 종료 신호 → Join → Service 해제 → Pool Shutdown).
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

	return 0;
}