#include "World/Level.h"

#include "Network/ClientPacketHandler.h"
#include "Network/GameServerSession.h"
#include "UCollisionMap.h"
#include "Iocp/Session.h"
#include "Core/DiagCounters.h"

#include <iostream>

void Level::Init(const std::string& CollisionCsvPath, int32 InLevelID)
{
	LevelID = InLevelID;
	Queue = std::make_shared<JobQueue>();

	// CollisionMap 을 자체 로드한다 (다른 Level 과 공유하지 않는다).
	// TODO : 이것도 함수로 따로 뺄만하고 CollisionMap Load 이런식으로
	auto Collision = std::make_shared<UCollisionMap>();
	const bool bLoaded = Collision->Load(CollisionCsvPath);
	if (bLoaded)
	{
		CollisionMap = std::move(Collision);
		bCollisionLoaded = true;
	}
	std::cout << "[Level " << LevelID << "] Init — CollisionMap "
		<< (bLoaded ? "loaded" : "failed") << ": " << CollisionCsvPath << "\n";

	// 고정 좌표에 Monster 1마리 스폰
	const uint64 MonsterID = NextMonsterID++;
	Monsters[MonsterID] = std::make_shared<AMonsterActor>(MonsterID, MonsterSpawnTileX, MonsterSpawnTileY, shared_from_this());
}

void Level::BeginPlay()
{
	std::cout << "[Level " << LevelID << "] BeginPlay\n";
}

void Level::Destroy()
{
	std::cout << "[Level " << LevelID << "] Destroy\n";
}

// ---------------------------------------------------------------------------
// 공개 API — 모두 Job 으로 큐잉하여 내부 상태를 단일 스레드 접근으로 보호한다.
// ---------------------------------------------------------------------------

void Level::Enter(uint64 PlayerID, std::shared_ptr<GameServerSession> Session)
{
	Queue->PushJob(std::make_shared<Job>([Self = shared_from_this(), PlayerID, Session]()
	{
		Self->DoEnter(PlayerID, std::move(Session));
	}));
}

void Level::Leave(uint64 PlayerID)
{
	Queue->PushJob(std::make_shared<Job>([Self = shared_from_this(), PlayerID]()
	{
		Self->DoLeave(PlayerID);
	}));
}

void Level::TryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq)
{
	Queue->PushJob(std::make_shared<Job>([Self = shared_from_this(), PlayerID, Dir, ClientSeq]()
	{
		Self->DoTryMove(PlayerID, Dir, ClientSeq);
	}));
}

bool Level::FindNearestPlayer(FTileNode InNode, uint64& OutTargetID, FTileNode& OutNode)
{
	uint64 BestID = 0;
	FTileNode BestTile { -1, -1 };
	int32 BestDist = INT_MAX;

	for (const auto& [ID, Tile] : Players)
	{
		FTileNode PlayerTile{Tile.TileX, Tile.TileY};
		const int32 Dist = ManhattanDistance(InNode, PlayerTile);
		if (Dist < BestDist) { BestDist = Dist; BestID = ID; BestTile = PlayerTile; }
	}
	OutNode = BestTile;
	OutTargetID = BestID;
	
	return true;
}

bool Level::CalculatePlayerTileByID(uint64 PlayerID, FTileNode& OutNode)
{
	auto It = Players.find(PlayerID);
	if (It != Players.end())
	{
		OutNode.X = It->second.TileX;
		OutNode.Y = It->second.TileY;
		return true;
	}
	return false;	
}

void Level::Broadcast(SendBufferRef Buffer, uint64 ExceptID)
{
	Queue->PushJob(std::make_shared<Job>([Self = shared_from_this(), Buffer, ExceptID]()
	{
		Self->DoBroadcast(Buffer, ExceptID);
	}));
}

void Level::PushTickJob()
{
	Queue->PushJob(std::make_shared<Job>([Self = shared_from_this()]()
	{
		Self->DoTick();
	}));
}

// ---------------------------------------------------------------------------
// 내부 구현 — JobQueue 안에서만 실행. 락 없음.
// ---------------------------------------------------------------------------

void Level::DoEnter(uint64 PlayerID, std::shared_ptr<GameServerSession> Session)
{
	// 스폰 좌표 계산
	const int32 TileX = SpawnBaseTileX + static_cast<int32>((PlayerID - 1) * SpawnStrideX);
	const int32 TileY = SpawnBaseTileY;

	PlayerEntry NewEntry;
	NewEntry.PlayerID = PlayerID;
	NewEntry.TileX = TileX;
	NewEntry.TileY = TileY;
	NewEntry.HP = PlayerMaxHP;
	NewEntry.Session = Session;

	// 기존 플레이어 스냅샷 수집 (자신 추가 전)
	std::vector<PlayerEntry> Others;
	Others.reserve(Players.size());
	for (const auto& Pair : Players)
		Others.push_back(Pair.second);

	// 플레이어 등록
	Players.emplace(PlayerID, std::move(NewEntry));

	std::cout << "[Level " << LevelID << "] Enter: id=" << PlayerID
		<< " spawn=(" << TileX << "," << TileY << ")"
		<< " others=" << Others.size() << "\n";

	// S_ENTER_GAME 응답 — 본인 + 기존 플레이어 + 몬스터
	Protocol::S_ENTER_GAME EnterRes;
	EnterRes.set_player_id(PlayerID);
	EnterRes.set_tile_x(TileX);
	EnterRes.set_tile_y(TileY);
	EnterRes.set_room_id(static_cast<uint32>(LevelID));
	for (const auto& Entry : Others)
	{
		Protocol::PlayerInfo* Info = EnterRes.add_others();
		Info->set_player_id(Entry.PlayerID);
		Info->set_tile_x(Entry.TileX);
		Info->set_tile_y(Entry.TileY);
	}
	for (const auto& [MID, Monster] : Monsters)
	{
		Protocol::MonsterInfo* MI = EnterRes.add_monsters();
		MI->set_monster_id(MID);
		MI->set_tile_x(Monster->GetTileX());
		MI->set_tile_y(Monster->GetTileY());
	}
	Session->Send(ClientPacketHandler::MakeSendBuffer(EnterRes));

	// S_SPAWN 브로드캐스트 — 기존 접속자들에게 신규 입장자 알림
	Protocol::S_SPAWN SpawnPkt;
	SpawnPkt.set_player_id(PlayerID);
	SpawnPkt.set_tile_x(TileX);
	SpawnPkt.set_tile_y(TileY);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(SpawnPkt), PlayerID);
}

void Level::DoLeave(uint64 PlayerID)
{
	Players.erase(PlayerID);
	for (auto& [MID, Monster] : Monsters)
		Monster->OnPlayerLeft(PlayerID);
}

void Level::DoTick()
{
	if (Players.empty())
		return;

	// 플레이어 타일 목록 수집
	std::vector<std::pair<uint64, FTileNode>> PlayerTiles;
	PlayerTiles.reserve(Players.size());
	for (const auto& [PID, Entry] : Players)
		PlayerTiles.push_back({ PID, { Entry.TileX, Entry.TileY } });

	for (auto& [MID, Monster] : Monsters)
	{
		Monster->ServerTick(TickIntervalMs);
	}
}

void Level::BroadcastMonsterMove(uint64 MonsterID, int32 TileX, int32 TileY)
{
	if (MonsterID == 0)
	{
		std::cout << "MonsterID가 유효하지 않습니다." << MonsterID << "\n";
		return;
	}
	
	Protocol::S_MONSTER_MOVE Pkt;
	Pkt.set_monster_id(MonsterID);
	Pkt.set_tile_x(TileX);
	Pkt.set_tile_y(TileY);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::BroadcastMonsterAttack(uint64 MonsterID, uint64 TargetPlayerID)
{
	if (MonsterID == 0)
	{
		std::cout << "MonsterID가 유효하지 않습니다." << MonsterID << "\n";
		return;
	}
	
	if (TargetPlayerID == 0)
	{
		std::cout << "TargetPlayerID가 유효하지 않습니다." << TargetPlayerID << "\n";
		return;
	}
	
	Protocol::S_MONSTER_ATTACK Pkt;
	Pkt.set_monster_id(MonsterID);
	Pkt.set_target_player_id(TargetPlayerID);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);

	auto It = Players.find(TargetPlayerID);
	if (It == Players.end())
		return;

	PlayerEntry& Target = It->second;
	Target.HP -= AMonsterActor::GetAttackDamage();
	if (Target.HP < 0) Target.HP = 0;

	BroadcastPlayerDamaged(TargetPlayerID, Target.HP);
	if (Target.HP <= 0)
		BroadcastPlayerDied(TargetPlayerID);
}

void Level::BroadcastPlayerDamaged(uint64 PlayerID, int32 HP)
{
	Protocol::S_PLAYER_DAMAGED Pkt;
	Pkt.set_player_id(PlayerID);
	Pkt.set_hp(HP);
	Pkt.set_max_hp(PlayerMaxHP);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::BroadcastPlayerDied(uint64 PlayerID)
{
	Protocol::S_PLAYER_DIED Pkt;
	Pkt.set_player_id(PlayerID);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq)
{
	if (LevelID >= 0 && LevelID < static_cast<int32>(DIAG_ROOM_COUNT))
		GRoomTryMoveCount[LevelID].fetch_add(1, std::memory_order_relaxed);

	// 방향 → 델타 변환
	int32 DeltaX = 0;
	int32 DeltaY = 0;
	switch (Dir)
	{
	case Protocol::DIR_UP:    DeltaY = -1; break;
	case Protocol::DIR_DOWN:  DeltaY =  1; break;
	case Protocol::DIR_LEFT:  DeltaX = -1; break;
	case Protocol::DIR_RIGHT: DeltaX =  1; break;
	default: break;
	}

	auto It = Players.find(PlayerID);
	if (It == Players.end())
		return;
	
	// TODO : 타일 검증 판정이 필요

	PlayerEntry& Self = It->second;
	const int32 NextTileX = Self.TileX + DeltaX;
	const int32 NextTileY = Self.TileY + DeltaY;

	// 위치 갱신
	Self.TileX = NextTileX;
	Self.TileY = NextTileY;

	// S_MOVE 브로드캐스트
	Protocol::S_MOVE MovePkt;
	MovePkt.set_player_id(PlayerID);
	MovePkt.set_tile_x(NextTileX);
	MovePkt.set_tile_y(NextTileY);
	MovePkt.set_dir(Dir);
	MovePkt.set_client_seq(ClientSeq);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(MovePkt), 0);
}

void Level::DoBroadcast(SendBufferRef Buffer, uint64 ExceptID)
{
	std::vector<std::shared_ptr<GameServerSession>> Targets;
	Targets.reserve(Players.size());
	for (const auto& Pair : Players)
	{
		if (Pair.first == ExceptID) continue;
		if (auto Locked = Pair.second.Session.lock())
			Targets.push_back(Locked);
	}

	const std::chrono::steady_clock::time_point T0 = std::chrono::steady_clock::now();
	for (const auto& Session : Targets)
		Session->Send(Buffer);
	const std::chrono::steady_clock::time_point T1 = std::chrono::steady_clock::now();
	const uint64 ElapsedNs = static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(T1 - T0).count());
	GDoBroadcastNsSum.fetch_add(ElapsedNs, std::memory_order_relaxed);
	GDoBroadcastCallCount.fetch_add(1, std::memory_order_relaxed);
	GDoBroadcastSessionSum.fetch_add(static_cast<uint64>(Targets.size()), std::memory_order_relaxed);

	DiagPushBroadcastNs(ElapsedNs); // LOG LOGIC : ring buffer 에 sample push (P95 계산용)
}


int32 Level::ManhattanDistance(const FTileNode& A, const FTileNode& B)
{
	return std::abs(A.X - B.X) + std::abs(A.Y - B.Y);
}