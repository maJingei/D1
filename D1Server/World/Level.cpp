#include "World/Level.h"

#include "DB/DBConnection.h"
#include "DB/DBContext.h"
#include "DB/DBJobQueue.h"
#include "LevelConfig.h"
#include "Network/ClientPacketHandler.h"
#include "Network/GameServerSession.h"
#include "World/World.h"
#include "UCollisionMap.h"
#include "Iocp/Session.h"

#include <iostream>
#include <utility>

void Level::Init(const std::string& CollisionCsvPath, int32 InLevelID, const FPortalConfig& InPortalConfig)
{
	LevelID = InLevelID;

	// CollisionMap 은 Level 당 1개 — 다른 Level 과 공유하지 않는다.
	LoadCollisionMap(CollisionCsvPath);

	std::shared_ptr<Level> Self = std::static_pointer_cast<Level>(shared_from_this());

	// LevelConfig.h::LevelMonsterSpawns[LevelID] 정적 테이블을 그대로 순회 — Level 별로 종류/좌표가 다르다.
	// Level1: Slime 1마리 (10,10) / Level2: Slime/Minotaur/RedOrc/GreenOrc 4마리.
	if (LevelID >= 0 && LevelID < AVAILABLE_LEVEL_COUNT)
	{
		const MonsterSpawnTable& SpawnTable = LevelMonsterSpawns[LevelID];
		for (int32 i = 0; i < SpawnTable.Count; ++i)
		{
			const MonsterSpawnInfo& Info = SpawnTable.Entries[i];
			const uint64 MonsterID = NextMonsterID++;
			Monsters[MonsterID] = std::make_shared<AMonsterActor>(MonsterID, Info.TileX, Info.TileY, Self, Info.Type);
		}
	}

	// Portal 1개 스폰 — 좌표/대상은 World 가 Level 별로 주입한 FPortalConfig 에서 받는다.
	const uint64 PortalID = NextPortalID++;
	Portals[PortalID] = std::make_shared<APortalActor>(PortalID, InPortalConfig, Self);
}

void Level::LoadCollisionMap(const std::string& CollisionCsvPath)
{
	auto Collision = std::make_shared<UCollisionMap>();
	const bool bLoaded = Collision->Load(CollisionCsvPath);
	if (bLoaded)
	{
		CollisionMap = std::move(Collision);
		bCollisionLoaded = true;

		// Walkable 타일 좌표 전체 스캔 precompute — World::EnterAnyLevel 이 PlayerID % size 로 스폰 타일을 뽑는 소스.
		WalkableTiles.clear();
		const int32 Rows = CollisionMap->GetRows();
		const int32 Cols = CollisionMap->GetCols();
		WalkableTiles.reserve(static_cast<size_t>(Rows) * static_cast<size_t>(Cols));
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			for (int32 X = 0; X < Cols; ++X)
			{
				if (!CollisionMap->IsBlocked(X, Y))
					WalkableTiles.emplace_back(X, Y);
			}
		}
	}
}

void Level::BeginPlay()
{
}

void Level::Destroy()
{
}

// ---------------------------------------------------------------------------
// 공개 API 래퍼는 제거됨. 외부 호출자는 DoAsync(&Level::Do_, args...) 로 직접 큐잉한다.
// ---------------------------------------------------------------------------

bool Level::FindNearestPlayer(FTileNode InNode, uint64& OutTargetID, FTileNode& OutNode)
{
	uint64 BestID = 0;
	FTileNode BestTile { -1, -1 };
	int32 BestDist = INT_MAX;

	for (const auto& [ID, Tile] : Players)
	{
		// 사망 상태 플레이어는 관전 세션으로 남아있을 뿐이므로 몬스터 타겟 후보에서 제외.
		if (Tile.bIsDead) continue;
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

// ---------------------------------------------------------------------------
// 내부 구현 — DoAsync 로만 큐잉되어 실행. JobQueue 직렬화로 보호, 별도 락 없음.
// ---------------------------------------------------------------------------

void Level::DoEnter(uint64 PlayerID, std::shared_ptr<GameServerSession> Session, int32 TileX, int32 TileY, bool bFromPortal)
{
	// 디폴트 PlayerEntry 를 구성해 공통 DoEnterWithState 경로로 위임.
	PlayerEntry NewEntry;
	NewEntry.PlayerID = PlayerID;
	NewEntry.TileX = TileX;
	NewEntry.TileY = TileY;
	NewEntry.TileMoveSpeed = DefaultPlayerTileMoveSpeed;
	NewEntry.bJustTeleported = bFromPortal;
	// 레거시 진입 경로(Handle_C_ENTER_GAME 폴백). 사람 클라 디폴트는 신규 4방향 PlayerSprite(CT_DEFAULT).
	NewEntry.CharacterType = Protocol::CT_DEFAULT;

	DoEnterWithState(std::move(NewEntry), std::move(Session));
}

void Level::DoEnterWithState(PlayerEntry InitialEntry, std::shared_ptr<GameServerSession> Session)
{
	// 세션 weak_ptr 연결 — 호출부가 책임지지 않도록 여기서 일괄 세팅.
	InitialEntry.Session = Session;

	const uint64 PlayerID = InitialEntry.PlayerID;
	const int32 TileX = InitialEntry.TileX;
	const int32 TileY = InitialEntry.TileY;
	const Protocol::CharacterType CharType = InitialEntry.CharacterType;
	const float MoveSpeed = InitialEntry.TileMoveSpeed;
	// nameplate 는 emplace 후 InitialEntry 가 move 되므로 미리 값 복사 — S_ENTER_GAME(my)+S_SPAWN 양쪽에서 사용.
	const std::string MyNameplateText = InitialEntry.NameplateText;

	// 기존 플레이어 스냅샷 수집 (자신 추가 전).
	std::vector<PlayerEntry> Others;
	Others.reserve(Players.size());
	for (const auto& Pair : Players)
		Others.push_back(Pair.second);

	Players.emplace(PlayerID, std::move(InitialEntry));

	std::cout << "[Level " << LevelID << "] Enter: id=" << PlayerID
		<< " spawn=(" << TileX << "," << TileY << ") others=" << Others.size() << "\n";

	// S_ENTER_GAME 응답 — 본인 + 기존 플레이어 + 몬스터.
	Protocol::S_ENTER_GAME EnterRes;
	EnterRes.set_player_id(PlayerID);
	EnterRes.set_tile_x(TileX);
	EnterRes.set_tile_y(TileY);
	EnterRes.set_room_id(static_cast<uint32>(LevelID));
	EnterRes.set_move_speed(MoveSpeed);
	EnterRes.set_character_type(CharType);
	EnterRes.set_my_nameplate_text(MyNameplateText);
	for (const auto& Entry : Others)
	{
		Protocol::PlayerInfo* Info = EnterRes.add_others();
		Info->set_player_id(Entry.PlayerID);
		Info->set_tile_x(Entry.TileX);
		Info->set_tile_y(Entry.TileY);
		Info->set_character_type(Entry.CharacterType);
		Info->set_nameplate_text(Entry.NameplateText);
	}
	for (const auto& [MID, Monster] : Monsters)
	{
		Protocol::MonsterInfo* MI = EnterRes.add_monsters();
		MI->set_monster_id(MID);
		MI->set_tile_x(Monster->GetTileX());
		MI->set_tile_y(Monster->GetTileY());
		MI->set_monster_type(Monster->GetMonsterType());
	}
	Session->Send(ClientPacketHandler::MakeSendBuffer(EnterRes));

	// S_SPAWN 브로드캐스트 — 기존 접속자들에게 신규 입장자 알림.
	Protocol::S_SPAWN SpawnPkt;
	SpawnPkt.set_player_id(PlayerID);
	SpawnPkt.set_tile_x(TileX);
	SpawnPkt.set_tile_y(TileY);
	SpawnPkt.set_character_type(CharType);
	SpawnPkt.set_nameplate_text(MyNameplateText);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(SpawnPkt), PlayerID);
}

void Level::DoLeave(uint64 PlayerID)
{
    DoRemovePlayer(PlayerID);
}

void Level::DoLogoutAndSave(uint64 PlayerID, std::string AccountId)
{
	// 현재 Level.Players 에 있는 entry 가 로그아웃 시점의 진실. 힙 스냅샷을 떠서 DB 워커로 넘긴다.
	auto It = Players.find(PlayerID);
	if (It != Players.end())
	{
		// M6 AsNoTracking 업데이트 경로 — Players 맵이 소유한 원본과 독립된 힙 복사본을 shared_ptr 로 감싼다.
		// Players.erase 이후에도 SnapshotPtr 은 자기 복사본을 들고 있어 수명이 안전.
		auto SnapshotPtr = std::make_shared<PlayerEntry>(It->second);
		SnapshotPtr->LevelID = LevelID; // 방어적 동기화.

		DBJobQueue::GetInstance().Schedule([SnapshotPtr](DBConnection& Conn)
		{
			DBContext Ctx(Conn);
			auto& PlayersDB = Ctx.Set<PlayerEntry>();
			PlayersDB.Update(SnapshotPtr);
			const bool bOk = Ctx.SaveChanges();
			std::cout << "[DB] Logout save " << (bOk ? "OK" : "FAIL")
				<< " PlayerID=" << SnapshotPtr->PlayerID
				<< " tile=(" << SnapshotPtr->TileX << "," << SnapshotPtr->TileY << ")"
				<< " HP=" << SnapshotPtr->HP << "/" << SnapshotPtr->MaxHP << "\n";
		});
	}

	DoRemovePlayer(PlayerID);

	if (AccountId.empty() == false)
		World::GetInstance().UnregisterAccount(AccountId);
}

void Level::DoRemovePlayer(uint64 PlayerID)
{
	Players.erase(PlayerID);
	for (auto& [MID, Monster] : Monsters)
		Monster->OnPlayerLeft(PlayerID);

	// 남은 플레이어들에게 퇴장 알림. 퇴장자는 이미 Players 에서 제거됐으므로 ExceptID=0 으로 충분.
	Protocol::S_PLAYER_LEFT LeftPkt;
	LeftPkt.set_player_id(PlayerID);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(LeftPkt), 0);
}

void Level::Tick(float DeltaTime)
{
	if (Players.empty())
		return;

	// Monster::ServerTick 은 아직 int32 ms 시그니처라 DeltaTime(초) 을 ms 로 변환해 전달.
	const int32 DeltaMs = static_cast<int32>(DeltaTime * 1000.0f);
	for (auto& [MID, Monster] : Monsters)
	{
		Monster->ServerTick(DeltaMs);
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
		// 관전 모드: Players 맵에는 Session 을 남겨 브로드캐스트(몬스터/타 플레이어 이동 등)를 계속 수신하게 한다.
		// 클라에서는 S_PLAYER_DIED 수신 시 해당 Actor 를 월드에서 제거하여 화면 상의 캐릭터만 증발한다.
		Target.bIsDead = true;
		BroadcastPlayerDied(TargetPlayerID);

		// 이미 락온된 몬스터들의 타겟을 해제한다. FindNearestPlayer 필터는 새 타겟 탐색에만 적용되므로 별도로 필요.
		for (auto& [MID, Monster] : Monsters)
			Monster->OnPlayerLeft(TargetPlayerID);
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

	// 공격 액션 브로드캐스트 — 헛스윙/히트 무관하게 항상 송신. 본인은 이미 로컬 애니메이션 재생 중이므로 ExceptID=PlayerID 로 제외.
	Protocol::S_PLAYER_ATTACK AttackPkt;
	AttackPkt.set_player_id(PlayerID);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(AttackPkt), PlayerID);

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

	// 3) Player 간 충돌 검사 — 자기 자신과 사망자는 제외하고, 살아있는 다른 플레이어가 점유 중이면 차단.
	for (const auto& Pair : Players)
	{
		if (Pair.first == Self.PlayerID)
			continue;
		if (Pair.second.bIsDead)
			continue;
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

void Level::DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq, uint64 /*ClientDeltaMs*/)
{
	auto It = Players.find(PlayerID);
	if (It == Players.end())
		return;

	PlayerEntry& Self = It->second;

	// 사망 상태 플레이어의 이동은 무시한다 (관전 모드).
	if (Self.bIsDead)
		return;

	// ClientSeq 단조성 가드 — 이미 수락했던 seq 이하의 값이 오면 무시.
	if (ClientSeq <= Self.LastAcceptedSeq)
		return;

	// F8 디버그 시연 — burst 송신을 위해 cooldown 면제, 그리고 N 번째 패킷 강제 reject. _DEBUG 빌드에서만 분기에 실제 들어간다.
	bool bDebugSkipCooldown = false;
#ifdef _DEBUG
	if (Self.DebugCooldownBypassRemaining > 0)
	{
		bDebugSkipCooldown = true;
		--Self.DebugCooldownBypassRemaining;
	}
	if (Self.DebugForceRejectCountdown > 0)
	{
		--Self.DebugForceRejectCountdown;
		if (Self.DebugForceRejectCountdown == 0)
		{
			// reject 발급 후에는 LastServerAcceptMs/LastAcceptedSeq 갱신 없이 즉시 return — 클라가 OnServerMoveRejected
			// → snap → StageReplayFromSnapshots 흐름을 그대로 타며, 큐에 남은 후속 Dir 들은 4 배속 replay 로 따라잡는다.
			Protocol::S_MOVE_REJECT RejectPkt;
			RejectPkt.set_player_id(PlayerID);
			RejectPkt.set_tile_x(Self.TileX);
			RejectPkt.set_tile_y(Self.TileY);
			RejectPkt.set_client_seq(ClientSeq);
			RejectPkt.set_last_accepted_seq(Self.LastAcceptedSeq);
			if (auto Locked = Self.Session.lock())
			{
				Locked->Send(ClientPacketHandler::MakeSendBuffer(RejectPkt));
			}
			return;
		}
	}
#endif

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

	// 1) Cooldown 검증 — 서버 시각만 사용한다. 클라가 보고한 ClientDelta 는 무시(위조 가능).
	//    ServerDelta = (현재 서버 ms) - (마지막으로 수락된 C_MOVE 처리 시점). LastServerAcceptMs == 0 은 첫 패킷이므로 면제.
	//    NetworkJitter 로 인한 살짝 빠른 도착은 JitterMargin 만큼 너그럽게 허용.
	const uint64 NowMs = ::GetTickCount64();
	const float CooldownMs = 1000.0f / Self.TileMoveSpeed;
	static constexpr int64 JitterMarginMs = 100;

	if (!bDebugSkipCooldown && Self.LastServerAcceptMs != 0)
	{
		const uint64 ServerDelta = NowMs - Self.LastServerAcceptMs;
		if (static_cast<float>(ServerDelta) + static_cast<float>(JitterMarginMs) < CooldownMs)
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
	}

	// 2. 공간검증
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

	// 검증 통과 — 위치 + Facing + 수락 seq + 서버측 수락 시각 갱신 후 브로드캐스트.
	Self.TileX = NextTileX;
	Self.TileY = NextTileY;
	Self.LastDir = Dir;
	Self.LastAcceptedSeq = ClientSeq;
	Self.LastServerAcceptMs = NowMs;

	Protocol::S_MOVE MovePkt;
	MovePkt.set_player_id(PlayerID);
	MovePkt.set_tile_x(NextTileX);
	MovePkt.set_tile_y(NextTileY);
	MovePkt.set_dir(Dir);
	MovePkt.set_client_seq(ClientSeq);
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(MovePkt), 0);

	// Portal 트리거 검사 — 직전 Portal 전이로 인한 루프를 방지하기 위해 bJustTeleported 는 이동 1회당 1번 소모된다.
	if (Self.bJustTeleported)
	{
		Self.bJustTeleported = false;
		return;
	}

	// 새 위치에 Portal 이 있으면 전이 처리. Portal 컨테이너는 작고 이동 성공 빈도도 제한적이므로 선형 탐색 허용.
	for (const auto& [PID, Portal] : Portals)
	{
		if (Portal->GetTileX() == NextTileX && Portal->GetTileY() == NextTileY)
		{
			DoPortalTransition(PlayerID, Portal);
			return;
		}
	}
}

void Level::DoPortalTransition(uint64 PlayerID, std::shared_ptr<APortalActor> Portal)
{
	auto It = Players.find(PlayerID);
	if (It == Players.end())
		return;

	// Entry 에서 Session 을 먼저 lock — DoRemovePlayer 내부 erase 이후엔 weak_ptr 경로가 사라진다.
	std::shared_ptr<GameServerSession> Session = It->second.Session.lock();

	const int32 TargetLevelID = Portal->GetTargetLevelID();
	const int32 TargetTileX = Portal->GetTargetSpawnTileX();
	const int32 TargetTileY = Portal->GetTargetSpawnTileY();

	std::cout << "[Level " << LevelID << "] PortalTransition: id=" << PlayerID
		<< " target=(" << TargetLevelID << "," << TargetTileX << "," << TargetTileY << ")\n";

	// Portal 전후 PlayerEntry 영속 필드(NameplateText/HP/MaxHP/AttackDamage/TileMoveSpeed/CharacterType 등)를 보존하기 위해
	// erase 전에 현재 entry 를 스냅샷. spawn 좌표와 bJustTeleported 만 덮어쓴다.
	PlayerEntry Snapshot = It->second;
	Snapshot.TileX = TargetTileX;
	Snapshot.TileY = TargetTileY;
	Snapshot.bJustTeleported = true;
	Snapshot.LevelID = TargetLevelID;
	Snapshot.LastAcceptedSeq = 0;
	// 새 Level 첫 C_MOVE 는 cooldown 면제 — LastServerAcceptMs=0 이 첫 패킷 표지 역할.
	Snapshot.LastServerAcceptMs = 0;

	// 현재 Level 에서 퇴장 — Players 제거 + 몬스터 락온 해제 + S_PLAYER_LEFT 브로드캐스트 일괄 처리.
	DoRemovePlayer(PlayerID);

	// Session 이 이미 사라진 경우엔 전이 대상만 정리하고 종료.
	if (Session == nullptr)
		return;

	// 이동자 본인에게 Portal 전이 신호 — 클라는 이 패킷 수신 시 현재 월드를 비우고 후속 S_ENTER_GAME 을 기다린다.
	Protocol::S_PORTAL_TELEPORT TeleportPkt;
	TeleportPkt.set_new_level_id(static_cast<uint32>(TargetLevelID));
	TeleportPkt.set_spawn_tile_x(TargetTileX);
	TeleportPkt.set_spawn_tile_y(TargetTileY);
	Session->Send(ClientPacketHandler::MakeSendBuffer(TeleportPkt));

	// 세션의 LevelID 를 선갱신 — 이후 C_MOVE/C_ATTACK 이 들어오면 바로 대상 Level 로 라우팅되도록.
	Session->SetLevelID(TargetLevelID);

	// 대상 Level 의 JobQueue 에 DoEnterWithState 큐잉 — Snapshot 으로 영속 상태 보존.
	// DoEnter(디폴트 entry 생성 경로)를 거치지 않으므로 NameplateText/HP/MaxHP 등이 reset 되지 않는다.
	auto TargetLevel = World::GetInstance().GetLevel(TargetLevelID);
	if (TargetLevel != nullptr)
		TargetLevel->DoAsync(&Level::DoEnterWithState, std::move(Snapshot), Session);
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

void Level::DoSetDebugForceReject(uint64 PlayerID, uint32 NthPacket, uint32 BypassCount)
{
#ifdef _DEBUG
	auto It = Players.find(PlayerID);
	if (It == Players.end())
	{
		return;
	}
	// NthPacket==0 또는 BypassCount==0 은 비활성 의미라 무시. burst 시연이 아닌 잘못된 페이로드 방어.
	if (NthPacket == 0 || BypassCount == 0)
	{
		return;
	}
	// reject 위치(N) 가 burst 범위(M) 를 벗어나면 그 burst 안에 reject 시점이 없다는 뜻이라 무의미 — 무시.
	if (NthPacket > BypassCount)
	{
		return;
	}
	It->second.DebugForceRejectCountdown = NthPacket;
	It->second.DebugCooldownBypassRemaining = BypassCount;
#else
	(void)PlayerID;
	(void)NthPacket;
	(void)BypassCount;
#endif
}

void Level::DoBroadcastChat(uint64 SenderID, std::string Text)
{
	// 보낸 본인도 포함해 broadcast — 클라가 자신의 입력이 실제로 서버를 거쳐 돌아온 것을 채팅창에서 직접 확인.
	Protocol::S_CHAT Pkt;
	Pkt.set_sender_id(SenderID);
	Pkt.set_text(std::move(Text));
	DoBroadcast(ClientPacketHandler::MakeSendBuffer(Pkt), 0);
}


int32 Level::ManhattanDistance(const FTileNode& A, const FTileNode& B)
{
	return std::abs(A.X - B.X) + std::abs(A.Y - B.Y);
}