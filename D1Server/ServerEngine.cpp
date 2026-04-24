#include "ServerEngine.h"
#include "Iocp/SocketUtils.h"
#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Job/GlobalJobQueue.h"
#include "Network/ClientPacketHandler.h"
#include "World/World.h"
#include "DB/DBConnection.h"
#include "DB/DBConnectionPool.h"
#include "DB/DBJobQueue.h"
#include "DB/DBStatement.h"
#include "DB/DBContext.h"
#include "DB/DBMeta.h"
#include "DB/DBBuildSql.h"
#include "World/Level.h"
#include "World/PlayerEntry.h"

#include <cassert>
#include <cstring>
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

	// Smoke 쿼리 — DB 워커가 기동되면 현재 DB/계정/서버명을 한 번 찍어
	//   (a) ODBC 가 정말 GameDB 컨텍스트를 잡았는지
	//   (b) DBJobQueue → DB 워커 → DBConnection → SQL Server 전체 경로가 동작하는지
	// 를 육안으로 검증한다. 여기서 스케줄만 해두면 워커 스레드가 Launch 된 뒤 Drain 될 때 실행된다.
	DBJobQueue::GetInstance().Schedule([](DBConnection& Conn)
	{
		DBStatement Stmt(Conn.GetHandle());
		if (Stmt.IsValid() == false)
			return;

		if (Stmt.ExecuteDirect(L"SELECT DB_NAME(), SUSER_SNAME(), @@SERVERNAME") == false)
			return;

		if (Stmt.Fetch() == false)
			return;

		// DB_NAME/SUSER_SNAME/@@SERVERNAME 모두 sysname(nvarchar(128))이지만
		// 로컬 환경은 ASCII 범위라 narrow 채널로 가져와 std::cout 과 호환시킨다.
		char DbName[128] = { 0 };
		char UserName[128] = { 0 };
		char ServerName[128] = { 0 };
		Stmt.GetColumnString(1, DbName, sizeof(DbName));
		Stmt.GetColumnString(2, UserName, sizeof(UserName));
		Stmt.GetColumnString(3, ServerName, sizeof(ServerName));

		std::cout << "[DB][Smoke] CurrentDB=" << DbName
			<< " User=" << UserName
			<< " Server=" << ServerName << std::endl;
	});

	// M6 ChangeTracker + SaveChanges 검증 smoke — 실제 로그인 PlayerID 와 충돌 않게 전용 9999 사용.
	// "1 Job = 1 DBContext = 1 SaveChanges = 1 Transaction" 제약 때문에 각 단계를 독립 Job 으로 쪼갠다.
	// DONE 기준(육안 관측):
	//   Job1  이전 실행 찌꺼기 정리 (있으면 Remove+SaveChanges → DELETE, 없으면 skip)
	//   Job2  Add + 같은 Job 내 Find(PK) Added HIT + SaveChanges → INSERT
	//   Job3  Find MISS(DB 로드) → Find HIT(같은 ptr) → Remove + SaveChanges → DELETE
	//   Job4  Find 가 nullptr 로 정상 종료
	static constexpr uint64 kSmokePlayerID = 9999;

	// Job1: 이전 실행 찌꺼기 정리.
	DBJobQueue::GetInstance().Schedule([](DBConnection& Conn)
	{
		DBContext Ctx(Conn);
		auto& Players = Ctx.Set<PlayerEntry>();
		PlayerEntry* Old = Players.Find(kSmokePlayerID);
		if (Old == nullptr)
		{
			std::cout << "[M6/Smoke] Job1 cleanup: no leftover (PK=" << kSmokePlayerID << ")" << std::endl;
			return;
		}
		Players.Remove(Old);
		const bool bOk = Ctx.SaveChanges();
		std::cout << "[M6/Smoke] Job1 cleanup DELETE " << (bOk ? "OK" : "FAIL") << std::endl;
	});

	// Job2: Add + 같은 Job 내 Find(PK) Added HIT + SaveChanges.
	DBJobQueue::GetInstance().Schedule([](DBConnection& Conn)
	{
		DBContext Ctx(Conn);
		auto& Players = Ctx.Set<PlayerEntry>();

		// DBContext::Set 캐시 자체 검증 — 같은 타입 2회 요청 시 동일 인스턴스. Tracked 일관성의 전제.
		auto& PlayersAgain = Ctx.Set<PlayerEntry>();
		std::cout << "[M6/Smoke] Job2 Set<T> cache " << ((&Players == &PlayersAgain) ? "OK" : "FAIL") << std::endl;

		// Seed 행 — NOT NULL 컬럼 기본값만 채운다. NULL 가능 셋은 _Ind=SQL_NULL_DATA 디폴트 유지.
		auto Seed = std::make_shared<PlayerEntry>();
		Seed->PlayerID = kSmokePlayerID;
		Seed->CharacterType = Protocol::CT_DEFAULT;
		Seed->LevelID = 0;
		Seed->TileX = 5;
		Seed->TileY = 7;
		Seed->HP = 20;
		Seed->MaxHP = 20;
		Seed->AttackDamage = 5;
		Seed->TileMoveSpeed = 6.0f;
		PlayerEntry* Added = Players.Add(Seed);
		std::cout << "[M6/Smoke] Job2 Add ptr=" << static_cast<void*>(Added) << std::endl;

		// Added 상태의 엔티티는 같은 Job 내 Find(PK) 에서 HIT 로 잡혀야 한다 — 일관성 검증.
		PlayerEntry* Found = Players.Find(kSmokePlayerID);
		std::cout << "[M6/Smoke] Job2 Find-after-Add identity=" << ((Added == Found) ? "OK (same ptr)" : "FAIL") << std::endl;

		// 원자적 커밋 — Added → INSERT.
		const bool bOk = Ctx.SaveChanges();
		std::cout << "[M6/Smoke] Job2 SaveChanges (INSERT) " << (bOk ? "OK" : "FAIL") << std::endl;
	});

	// Job3: Find MISS(DB 로드) → Find HIT(같은 ptr) → Remove + SaveChanges (DELETE).
	DBJobQueue::GetInstance().Schedule([](DBConnection& Conn)
	{
		DBContext Ctx(Conn);
		auto& Players = Ctx.Set<PlayerEntry>();

		// Find #1 — 새 DBContext 라 Identified 비어있음 → [DBIdMap] MISS + DB SELECT 1회.
		PlayerEntry* First = Players.Find(kSmokePlayerID);
		std::cout << "[M6/Smoke] Job3 Find#1 ptr=" << static_cast<void*>(First)
			<< " TileX=" << (First ? First->TileX : -1) << std::endl;
		if (First == nullptr) return;

		// Find #2 — 동일 DBContext 내 재조회. DB 왕복 0회 + [DBIdMap] HIT + First 와 동일 ptr.
		PlayerEntry* Second = Players.Find(kSmokePlayerID);
		std::cout << "[M6/Smoke] Job3 Find#2 identity=" << ((First == Second) ? "OK (same ptr)" : "FAIL") << std::endl;

		// 삭제 → Deleted 상태 마킹 → SaveChanges 에서 DELETE.
		Players.Remove(First);
		const bool bOk = Ctx.SaveChanges();
		std::cout << "[M6/Smoke] Job3 SaveChanges (DELETE) " << (bOk ? "OK" : "FAIL") << std::endl;
	});

	// Job4: 삭제 후 재조회. DB 에도 행이 없으므로 nullptr 이어야 성공.
	DBJobQueue::GetInstance().Schedule([](DBConnection& Conn)
	{
		DBContext Ctx(Conn);
		auto& Players = Ctx.Set<PlayerEntry>();
		PlayerEntry* Gone = Players.Find(kSmokePlayerID);
		std::cout << "[M6/Smoke] Job4 Find post-DELETE ptr=" << static_cast<void*>(Gone) << " expected=nullptr" << std::endl;
	});

	// M4 smoke — 매크로 기반 메타데이터 / Build SQL 결과(? 플레이스홀더)를 main 스레드에서 한 번 덤프한다.
	// DB 워커를 타지 않는 메모리 전용 결과물이므로 Schedule 없이 직접 출력.
	// 위쪽 DBContext smoke 로그와 공존하며, 동일한 테이블 스키마를 두 경로가 공유함을 육안 검증한다.
	{
		const auto& Meta = GetTableMetadata<PlayerEntry>();
		std::cout << "[DB4][Meta] " << Meta.TableName << " (" << Meta.NumColumns << " cols)\n";
		for (size_t i = 0; i < Meta.NumColumns; ++i)
		{
			const auto& C = Meta.Columns[i];
			std::cout << "  [" << i << "] "
				<< C.Name << " " << C.SqlType
				<< (C.bIsPK ? " PK" : "")
				<< " offset=" << C.Offset
				<< " size=" << C.Size << "\n";
		}

		// wstring SQL 을 narrow 로그 채널에 흘리기 위한 ASCII 한정 변환 — 컬럼/테이블/? 만 들어가므로 안전.
		auto LogSql = [](const char* Label, const std::wstring& SqlW)
		{
			std::string SqlA;
			SqlA.reserve(SqlW.size());
			for (wchar_t C : SqlW) SqlA.push_back(static_cast<char>(C));
			std::cout << "[DB4][Build] " << Label << ":\n  " << SqlA << "\n";
		};

		LogSql("Insert     SQL", BuildInsertSql<PlayerEntry>());
		LogSql("SelectByPk SQL", BuildSelectByPkSql<PlayerEntry>());
		LogSql("Update     SQL", BuildUpdateSql<PlayerEntry>());
		LogSql("DeleteByPk SQL", BuildDeleteByPkSql<PlayerEntry>());
	}

	// World 초기화 — 각 Level 이 자신의 LevelID 에 해당하는 CollisionMap 을 로드한다.
	World::GetInstance().Init(ResourceBaseDir);

	// 재시작 후에도 PlayerID PK 충돌 없도록 DB 에서 MAX(PlayerID) 를 읽어 NextPlayerID 시딩.
	World::GetInstance().SeedNextPlayerIDFromDB();

	std::cout << "[Engine] Init\n";
	return true;
}

void ServerEngine::BeginPlay()
{
	World::GetInstance().BeginPlay();
	std::cout << "[Engine] BeginPlay\n";
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

	std::cout << "[Engine] Destroy\n";
}