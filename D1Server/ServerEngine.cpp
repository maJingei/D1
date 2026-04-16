#include "ServerEngine.h"
#include "Iocp/SocketUtils.h"
#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Job/GlobalJobQueue.h"
#include "Network/ClientPacketHandler.h"
#include "World/World.h"

#include <iostream>
#include <chrono>

bool ServerEngine::Init()
{
	// 싱글톤 매니저 생성 시점을 명시적으로 고정한다.
	ThreadManager::GetInstance();
	PoolManager::GetInstance();
	GlobalJobQueue::GetInstance();

	// 메인 스레드 TLS 초기화 (ID=1)
	ThreadManager::InitTLS();

	// PoolManager 초기화
	PoolManager::GetInstance().Initialize(64);

	// Winsock 초기화 + LPFN 바인딩
	SocketUtils::Init();

	// 서버 패킷 핸들러 테이블 초기화
	ClientPacketHandler::Init();

	// exe 기준 상대 경로 산출
	char ExePath[MAX_PATH] = { 0 };
	::GetModuleFileNameA(nullptr, ExePath, MAX_PATH);
	std::string Path(ExePath);
	const size_t LastSep = Path.find_last_of("\\/");
	const std::string BaseDir = (LastSep != std::string::npos) ? Path.substr(0, LastSep + 1) : std::string();
	const std::string CollisionCsvPath = BaseDir + "..\\..\\Resource\\Collision_Collision.csv";

	// World 초기화 — 각 Level 이 CollisionMap 을 자체 로드한다.
	World::GetInstance().Init(CollisionCsvPath);

	std::cout << "[Engine] Init\n";
	return true;
}

void ServerEngine::BeginPlay()
{
	World::GetInstance().BeginPlay();

	// TimerLoop 스레드 시작 — 20ms 주기로 각 Level 의 JobQueue 에 TickJob 을 push 한다.
	bRunning.store(true, std::memory_order_relaxed);
	TimerThread = std::thread([this]() { TimerLoop(); });

	std::cout << "[Engine] BeginPlay\n";
}

void ServerEngine::Destroy()
{
	// 1. TimerLoop 종료
	bRunning.store(false, std::memory_order_relaxed);
	if (TimerThread.joinable())
		TimerThread.join();
	std::cout << "[Engine] TimerLoop stopped\n";

	// 2. World → Level 종료
	World::GetInstance().Destroy();

	// 3. 싱글톤 매니저 정리
	PoolManager::GetInstance().Shutdown();
	ThreadManager::GetInstance().DestroyAllThreads();

	std::cout << "[Engine] Destroy\n";
}

void ServerEngine::TimerLoop()
{
	static constexpr int32 TickIntervalMs = 20;
	while (bRunning.load(std::memory_order_relaxed))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(TickIntervalMs));
		if (bRunning.load(std::memory_order_relaxed) == false)
			break;
		World::GetInstance().Tick();
	}
}