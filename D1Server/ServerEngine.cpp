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

	// PlayerEntry CRUD smoke — 고정 PlayerID 로 Insert→Find→Update→Find→Delete 를 순차 실행한다.
	//   (a) DBContext::Set<PlayerEntry>() 가 반환하는 DBSet<PlayerEntry> 의 4개 함수가 실제로
	//       dbo.PlayerEntry 와 왕복 SQL 을 주고받는지
	//   (b) Update 후 Find 가 갱신 값을 돌려주는지
	//   (c) Set<T>() 를 두 번 호출하면 같은 인스턴스가 돌아오는지 (M8 Identity Map 전제)
	// 를 로그 한 블록으로 확인. 실패해도 서버 부팅은 막지 않는다 — 진단 로그만 찍힘.
	// dbo.PlayerEntry 테이블이 SSMS 에서 미리 생성돼 있어야 한다 (Schema/PlayerEntry.sql).
	static constexpr uint64 kSmokePlayerID = 1;
	DBJobQueue::GetInstance().Schedule([](DBConnection& Conn)
	{
		DBContext Ctx(Conn);
		auto& Players = Ctx.Set<PlayerEntry>();

		// 캐시 동작 임시 검증 — 같은 타입의 Set 두 번 요청 시 같은 인스턴스 반환되어야 한다.
		// (M8 Identity Map 일관성의 전제. 본 마일스톤 한정 검증, 다음 단계에서 제거 예정)
		[[maybe_unused]] auto& PlayersAgain = Ctx.Set<PlayerEntry>();
		assert(&Players == &PlayersAgain);

		// 초기 행 템플릿. Insert 가 실패(PK 중복)해도 후속 Find/Update/Delete 는 이어간다.
		PlayerEntry Entry;
		Entry.PlayerID = kSmokePlayerID;
		Entry.CharacterType = Protocol::CT_DEFAULT;
		Entry.LevelID = 0;
		Entry.TileX = 5;
		Entry.TileY = 7;
		Entry.HP = 20;
		Entry.MaxHP = 20;
		Entry.AttackDamage = 5;
		Entry.TileMoveSpeed = 6.0f;

		// (1) Insert — PK 중복이어도 진단 로그만 남기고 계속 진행.
		if (Players.Insert(Entry))
			std::cout << "[DBContext] Insert OK (PlayerID=" << kSmokePlayerID << ")" << std::endl;
		else
			std::cout << "[DBContext] Insert skipped (already exists or failed, continuing)" << std::endl;

		// (2) Find #1 — Insert 성공/기존 행 둘 다 여기서 값을 확인.
		PlayerEntry Loaded;
		if (Players.Find(kSmokePlayerID, Loaded) == false)
		{
			std::cout << "[DBContext] Find FAIL (PlayerID=" << kSmokePlayerID << ")" << std::endl;
			return;
		}
		std::cout << "[DBContext] Find OK (PlayerID=" << Loaded.PlayerID
			<< ", LevelID=" << Loaded.LevelID
			<< ", TileX=" << Loaded.TileX
			<< ", TileY=" << Loaded.TileY
			<< ", HP=" << Loaded.HP << "/" << Loaded.MaxHP << ")" << std::endl;

		// (3) Update — 위치/HP 를 변경해 다음 Find 에서 관찰 가능한 차이를 만든다.
		Loaded.TileX = 13;
		Loaded.TileY = 21;
		Loaded.HP = 9;
		if (Players.Update(Loaded) == false)
		{
			std::cout << "[DBContext] Update FAIL (PlayerID=" << kSmokePlayerID << ")" << std::endl;
			return;
		}
		std::cout << "[DBContext] Update OK (PlayerID=" << kSmokePlayerID << ")" << std::endl;

		// (4) Find #2 — Update 가 영속화되었는지 SELECT 로 재검증.
		PlayerEntry Reloaded;
		if (Players.Find(kSmokePlayerID, Reloaded) == false)
		{
			std::cout << "[DBContext] Find(after Update) FAIL" << std::endl;
			return;
		}
		std::cout << "[DBContext] Find OK (PlayerID=" << Reloaded.PlayerID
			<< ", LevelID=" << Reloaded.LevelID
			<< ", TileX=" << Reloaded.TileX
			<< ", TileY=" << Reloaded.TileY
			<< ", HP=" << Reloaded.HP << "/" << Reloaded.MaxHP << ")" << std::endl;

		// (5) Delete — smoke 를 idempotent 하게 종료해 다음 부팅에서도 Insert 부터 다시 시작 가능하게 한다.
		if (Players.Delete(kSmokePlayerID))
			std::cout << "[DBContext] Delete OK (PlayerID=" << kSmokePlayerID << ")" << std::endl;
		else
			std::cout << "[DBContext] Delete FAIL (PlayerID=" << kSmokePlayerID << ")" << std::endl;
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