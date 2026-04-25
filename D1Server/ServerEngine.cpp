#include "ServerEngine.h"
#include "Iocp/SocketUtils.h"
#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Job/GlobalJobQueue.h"
#include "Network/ClientPacketHandler.h"
#include "World/World.h"
#include "DB/DBConnectionPool.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <conio.h>

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

	// Resource 루트 디렉토리 — World 가 LevelFolders[] 와 조합해 Level 별 CSV 경로를 만든다.
	const std::string ResourceBaseDir = BaseDir + "..\\..\\Resource\\";

	// DB 연결 풀 초기화 — Login=Load 시점보다 먼저 바닥을 깔아둔다.
	// Driver 18 기본값이 Encrypt=Yes 이므로 로컬 테스트용 TrustServerCertificate=Yes 를 명시해야 한다.
	static constexpr int32 kDBPoolSize = 4;
	const wchar_t* ConnectString =
		L"Driver={ODBC Driver 18 for SQL Server};"
		L"Server=localhost\\SQLEXPRESS;"
		L"Database=GameDB;"
		L"Trusted_Connection=Yes;"
		L"TrustServerCertificate=Yes;";

	if (DBConnectionPool::GetInstance().Init(ConnectString, kDBPoolSize) == false)
	{
		std::cout << "[Engine] DB pool init failed\n";
		return false;
	}

	// World 초기화 — 각 Level 이 자신의 LevelID 에 해당하는 CollisionMap 을 로드한다.
	World::GetInstance().Init(ResourceBaseDir);

	// 재시작 후에도 PlayerID PK 충돌 없도록 DB 에서 MAX(PlayerID) 를 읽어 NextPlayerID 시딩.
	World::GetInstance().SeedNextPlayerIDFromDB();

	return true;
}

void ServerEngine::BeginPlay()
{
	World::GetInstance().BeginPlay();
}

void ServerEngine::Tick()
{
	static constexpr int32 TickIntervalMs = 20;
	auto LastTime = std::chrono::steady_clock::now();
	while (true)
	{
		// Enter 키(CR) 입력 감지 시에만 루프 탈출. 다른 키는 소비하고 무시.
		if (_kbhit() && _getch() == '\r')
			break;

		// 이전 Tick 이후 경과한 실제 시간(초) 을 측정해 World 에 전달. 고정 20ms 가 아니라 Client 와 동일한 DeltaTime 모델.
		const auto Now = std::chrono::steady_clock::now();
		const float DeltaTime = std::chrono::duration<float>(Now - LastTime).count();
		LastTime = Now;

		World::GetInstance().Tick(DeltaTime);
		std::this_thread::sleep_for(std::chrono::milliseconds(TickIntervalMs));
	}
}

void ServerEngine::Destroy()
{
	// World → Level 종료
	World::GetInstance().Destroy();

	// DB 풀 해제는 DB 워커 Join 이후 이 시점에서 수행. HDBC 해제가 ODBC 호출을 동반하지만
	// 이 시점엔 DB 워커가 이미 Join 되어 in-flight SQLExecute 가 없다.
	DBConnectionPool::GetInstance().Shutdown();

	// 싱글톤 매니저 정리
	PoolManager::GetInstance().Shutdown();
	ThreadManager::GetInstance().DestroyAllThreads();
}