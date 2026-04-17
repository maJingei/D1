#include "World/Level.h"

#include "Network/ClientPacketHandler.h"
#include "Network/GameServerSession.h"
#include "UCollisionMap.h"
#include "Iocp/Session.h"

#include <iostream>

void Level::Init(const std::string& CollisionCsvPath, int32 InLevelID)
{
	LevelID = InLevelID;
	Queue = std::make_shared<JobQueue>();

	// CollisionMap 은 Level 당 1개 — 다른 Level 과 공유하지 않는다.
	LoadCollisionMap(CollisionCsvPath);

	// 고정 좌표에 Monster 1마리 스폰
	const uint64 MonsterID = NextMonsterID++;
	Monsters[MonsterID] = std::make_shared<AMonsterActor>(MonsterID, MonsterSpawnTileX, MonsterSpawnTileY, shared_from_this());
}

void Level::LoadCollisionMap(const std::string& CollisionCsvPath)
{
	auto Collision = std::make_shared<UCollisionMap>();
	const bool bLoaded = Collision->Load(CollisionCsvPath);
	if (bLoaded)
	{
		CollisionMap = std::move(Collision);
		bCollisionLoaded = true;
	}
	std::cout << "[Level " << LevelID << "] CollisionMap " << (bLoaded ? "loaded" : "failed") << ": " << CollisionCsvPath << "\n";
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

void Level::TryAttack(uint64 PlayerID)
{
	Queue->PushJob(std::make_shared<Job>([Self = shared_from_this(), PlayerID]()
	{
		Self->DoTryAttack(PlayerID);
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

	// 스탯은 PlayerEntry 기본값(HP=20, MaxHP=20, AttackDamage=5, LastDir=DOWN)을 사용한다.
	// 향후 캐릭터 클래스/장비에 따라 달라질 수 있도록 인스턴스 단위 보유.
	PlayerEntry NewEntry;
	NewEntry.PlayerID = PlayerID;
	NewEntry.TileX = TileX;
	NewEntry.TileY = TileY;
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

	auto PlayerIt = Players.find(TargetPlayerID);
	if (PlayerIt == Players.end())
		return;

	PlayerEntry& Target = PlayerIt->second;

	// 사망 상태 플레이어는 추가 데미지를 받지 않는다 (관전 모드).
	if (Target.bIsDead)
		return;

	// 공격자(Monster) 인스턴스의 AttackDamage 를 사용 — 공격자 책임 원칙.
	auto MonsterIt = Monsters.find(MonsterID);
	if (MonsterIt == Monsters.end())
		return;

	const int32 Damage = MonsterIt->second->GetAttackDamage();
	Target.HP -= Damage;
	if (Target.HP < 0) Target.HP = 0;

	BroadcastPlayerDamaged(TargetPlayerID, Target.HP, Target.MaxHP);
	if (Target.HP <= 0)
	{
		Target.bIsDead = true;
		BroadcastPlayerDied(TargetPlayerID);
	}
}

void Level::BroadcastPlayerDamaged(uint64 PlayerID, int32 HP, int32 MaxHP)
{
	Protocol::S_PLAYER_DAMAGED Pkt;
	Pkt.set_player_id(PlayerID);
	Pkt.set_hp(HP);
	Pkt.set_max_hp(MaxHP);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::BroadcastPlayerDied(uint64 PlayerID)
{
	Protocol::S_PLAYER_DIED Pkt;
	Pkt.set_player_id(PlayerID);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::BroadcastMonsterDamaged(uint64 MonsterID, int32 HP, int32 MaxHP)
{
	Protocol::S_MONSTER_DAMAGED Pkt;
	Pkt.set_monster_id(MonsterID);
	Pkt.set_hp(HP);
	Pkt.set_max_hp(MaxHP);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::BroadcastMonsterDied(uint64 MonsterID)
{
	Protocol::S_MONSTER_DIED Pkt;
	Pkt.set_monster_id(MonsterID);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}

void Level::DoTryAttack(uint64 PlayerID)
{
	auto PlayerIt = Players.find(PlayerID);
	if (PlayerIt == Players.end())
		return;

	PlayerEntry& Self = PlayerIt->second;

	// 사망 상태 플레이어의 공격은 무시한다.
	if (Self.bIsDead)
		return;

	// 1. 마지막 이동 방향(LastDir)을 기준으로 1칸 앞 타일 좌표 산출.
	int32 DeltaX = 0;
	int32 DeltaY = 0;
	switch (Self.LastDir)
	{
	case Protocol::DIR_UP:    DeltaY = -1; break;
	case Protocol::DIR_DOWN:  DeltaY =  1; break;
	case Protocol::DIR_LEFT:  DeltaX = -1; break;
	case Protocol::DIR_RIGHT: DeltaX =  1; break;
	default: return;
	}

	const int32 TargetTileX = Self.TileX + DeltaX;
	const int32 TargetTileY = Self.TileY + DeltaY;

	// 2. 1칸 앞 타일에 있는 몬스터 검색. 없으면 헛스윙으로 종료(패킷 송신 없음).
	uint64 HitMonsterID = 0;
	std::shared_ptr<AMonsterActor> HitMonster;
	for (auto& [MID, Monster] : Monsters)
	{
		if (Monster->GetTileX() == TargetTileX && Monster->GetTileY() == TargetTileY)
		{
			HitMonsterID = MID;
			HitMonster = Monster;
			break;
		}
	}
	if (HitMonster == nullptr)
		return;

	// 3. 공격자(Player) 인스턴스의 AttackDamage 를 적용. 공격자 책임 원칙.
	const int32 RemainHP = HitMonster->ApplyDamage(Self.AttackDamage);

	// 4. HP/사망 결과 브로드캐스트. 사망 시 Monsters 맵에서 제거하여 다음 Tick 부터 추격/공격 대상에서 빠진다.
	BroadcastMonsterDamaged(HitMonsterID, RemainHP, HitMonster->GetMaxHP());
	if (HitMonster->IsDead())
	{
		// 사망한 몬스터에게 락온된 다른 몬스터가 있을 수 있으므로 OnPlayerLeft 와 동일한 패턴은 불필요 — Monsters 만 정리.
		Monsters.erase(HitMonsterID);
		BroadcastMonsterDied(HitMonsterID);
	}
}

bool Level::ValidateMove(const PlayerEntry& Self, int32 NextTileX, int32 NextTileY) const
{
	// 1) 인접 타일 제약 — 맨해튼 거리 1 만 허용. DIR enum 범위 밖으로 인한 델타 0 이나 혹시 모를 원거리 도약도 여기서 걸러진다.
	const int32 AbsDx = std::abs(NextTileX - Self.TileX);
	const int32 AbsDy = std::abs(NextTileY - Self.TileY);
	if (AbsDx + AbsDy != 1)
		return false;

	// 2) 충돌 맵 범위/차단 검사 — IsBlocked 는 범위 밖도 차단으로 반환한다.
	if (CollisionMap == nullptr || CollisionMap->IsBlocked(NextTileX, NextTileY))
		return false;

	// 3) 다른 플레이어 점유 검사.
	for (const auto& Pair : Players)
	{
		if (Pair.first == Self.PlayerID) continue;
		if (Pair.second.bIsDead) continue;
		if (Pair.second.TileX == NextTileX && Pair.second.TileY == NextTileY)
			return false;
	}

	// 4) 몬스터 점유 검사.
	for (const auto& Pair : Monsters)
	{
		if (Pair.second->GetTileX() == NextTileX && Pair.second->GetTileY() == NextTileY)
			return false;
	}

	return true;
}

void Level::DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq)
{
	auto It = Players.find(PlayerID);
	if (It == Players.end())
		return;

	PlayerEntry& Self = It->second;

	// 사망 상태 플레이어의 이동은 무시한다 (관전 모드).
	if (Self.bIsDead)
		return;

	// 방향 → 델타 변환. 유효하지 않은 enum 값은 ValidateMove 의 인접 타일 검사에서 걸러진다.
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

	const int32 NextTileX = Self.TileX + DeltaX;
	const int32 NextTileY = Self.TileY + DeltaY;

	// -------------------------------------------------------------------
	// Client Prediction 모델 — 검증 실패 시 이동자에게만 S_MOVE_REJECT, 성공 시 레벨 전체에 S_MOVE 브로드캐스트.
	// -------------------------------------------------------------------
	if (!ValidateMove(Self, NextTileX, NextTileY))
	{
		Protocol::S_MOVE_REJECT RejectPkt;
		RejectPkt.set_player_id(PlayerID);
		RejectPkt.set_tile_x(Self.TileX);
		RejectPkt.set_tile_y(Self.TileY);
		RejectPkt.set_client_seq(ClientSeq);
		RejectPkt.set_last_accepted_seq(Self.LastAcceptedSeq);
		if (auto Locked = Self.Session.lock())
			Locked->Send(ClientPacketHandler::MakeSendBuffer(RejectPkt));
		return;
	}

	// 검증 통과 — 위치 + Facing + 수락 seq 갱신 후 브로드캐스트.
	Self.TileX = NextTileX;
	Self.TileY = NextTileY;
	Self.LastDir = Dir;
	Self.LastAcceptedSeq = ClientSeq;

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

	for (const auto& Session : Targets)
		Session->Send(Buffer);
}


int32 Level::ManhattanDistance(const FTileNode& A, const FTileNode& B)
{
	return std::abs(A.X - B.X) + std::abs(A.Y - B.Y);
}