#include "ServerPacketHandler.h"
#include "Game.h"
#include "World/UWorld.h"
#include "GameObject/APlayerActor.h"
#include "GameObject/AMonsterActor.h"

#include <cstdio>
#include <cwchar>


namespace
{
	/** 화면 좌상단 오버레이로 한 줄을 찍는다. */
	void ScreenLog(const wchar_t* Format, ...)
	{
		Game* Instance = Game::GetInstance();
		if (Instance == nullptr) return;

		wchar_t Buffer[256];
		va_list Args;
		va_start(Args, Format);
		std::vswprintf(Buffer, sizeof(Buffer) / sizeof(Buffer[0]), Format, Args);
		va_end(Args);

		Instance->AddDebugLog(Buffer);
	}

	/** World 내 APlayerActor 중 지정 PlayerID 를 가진 액터를 찾는다. 없으면 nullptr. */
	std::shared_ptr<APlayerActor> FindPlayerActor(UWorld* World, uint64 PlayerID)
	{
		if (World == nullptr) return nullptr;
		for (const std::shared_ptr<AActor>& Actor : World->GetActorsForIteration())
		{
			auto Player = std::dynamic_pointer_cast<APlayerActor>(Actor);
			if (Player && Player->GetPlayerID() == PlayerID)
				return Player;
		}
		return nullptr;
	}

	/** World 내 AMonsterActor 중 지정 MonsterID 를 가진 액터를 찾는다. 없으면 nullptr. */
	std::shared_ptr<AMonsterActor> FindMonsterActor(UWorld* World, uint64 MonsterID)
	{
		if (World == nullptr) return nullptr;
		for (const std::shared_ptr<AActor>& Actor : World->GetActorsForIteration())
		{
			auto Monster = std::dynamic_pointer_cast<AMonsterActor>(Actor);
			if (Monster && Monster->GetMonsterID() == MonsterID)
				return Monster;
		}
		return nullptr;
	}
}

PacketHandlerFunc GPacketHandler[UINT16_MAX];

bool Handle_INVALID(PacketSessionRef& /*session*/, BYTE* buffer, int32 /*len*/)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	ScreenLog(L"[Client] Invalid packet id=%u", header->Id);
	return false;
}

bool Handle_S_LOGIN(PacketSessionRef& /*session*/, Protocol::S_LOGIN& pkt)
{
	if (pkt.result() == 0)
		ScreenLog(L"[Client] LOGIN success: userId=%llu", pkt.user_id());
	else
		ScreenLog(L"[Client] LOGIN failed: result=%u", pkt.result());
	return true;
}

bool Handle_S_ENTER_GAME(PacketSessionRef& /*session*/, Protocol::S_ENTER_GAME& pkt)
{
	// 서버가 발급한 내 PlayerID와 스폰 좌표로 본인 PlayerActor를 월드에 생성한다.
	// 응답 payload의 others 목록은 이미 필드에 있던 다른 플레이어들이므로 함께 스폰한다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	// 1. 본인 스폰 — 서버가 지정한 타일 좌표 사용.
	// SetMyPlayerID 를 SpawnActor 보다 먼저 호출해야 APlayerActor::IsLocallyControlled() 가
	// 첫 Tick 전까지 유효한 비교 대상을 갖는다 (런타임 비교 기반 SSOT).
	const uint64 MyPlayerID = pkt.player_id();
	const int32 MyTileX = pkt.tile_x();
	const int32 MyTileY = pkt.tile_y();
	Instance->SetMyPlayerID(MyPlayerID);
	World->SpawnActor<APlayerActor>(MyPlayerID, MyTileX, MyTileY);

	ScreenLog(L"[Client] S_ENTER_GAME myId=%llu tile=(%d,%d) others=%d",
		MyPlayerID, MyTileX, MyTileY, pkt.others_size());

	// 2. 기존 접속자 스냅샷 — 신규 입장 시점에 이미 필드에 있던 플레이어들을 스폰
	for (int32 i = 0; i < pkt.others_size(); ++i)
	{
		const Protocol::PlayerInfo& Info = pkt.others(i);
		World->SpawnActor<APlayerActor>(Info.player_id(), Info.tile_x(), Info.tile_y());
	}

	// 3. 몬스터 스냅샷 — Level 에 존재하는 몬스터를 한 번에 스폰
	for (int32 i = 0; i < pkt.monsters_size(); ++i)
	{
		const Protocol::MonsterInfo& MI = pkt.monsters(i);
		World->SpawnActor<AMonsterActor>(MI.monster_id(), MI.tile_x(), MI.tile_y());
		ScreenLog(L"[Client] S_ENTER_GAME monster id=%llu tile=(%d,%d)", MI.monster_id(), MI.tile_x(), MI.tile_y());
	}

	return true;
}

bool Handle_S_SPAWN(PacketSessionRef& /*session*/, Protocol::S_SPAWN& pkt)
{
	// 기존 접속자에게만 수신되는 브로드캐스트. 신규 입장자의 Actor를 월드에 추가한다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	ScreenLog(L"[Client] S_SPAWN id=%llu tile=(%d,%d)",
		pkt.player_id(), pkt.tile_x(), pkt.tile_y());

	World->SpawnActor<APlayerActor>(pkt.player_id(), pkt.tile_x(), pkt.tile_y());
	return true;
}

bool Handle_S_MOVE(PacketSessionRef& /*session*/, Protocol::S_MOVE& pkt)
{
	// 서버가 확정한 이동. 본인 예측 seq 와 일치하면 APlayerActor 내부에서 pop 만, 그 외에는 BeginMoveTo.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr)
	{
		ScreenLog(L"[Client] S_MOVE for unknown player id=%llu", pkt.player_id());
		return true;
	}

	Player->OnServerMoveAccepted(pkt.tile_x(), pkt.tile_y(), pkt.dir(), pkt.client_seq());
	return true;
}

bool Handle_S_MOVE_REJECT(PacketSessionRef& /*session*/, Protocol::S_MOVE_REJECT& pkt)
{
	// 이동 거절 — 요청자에게만 수신되는 단일 전송. LastAcceptedSeq 로 스냅샷 clip + 서버 좌표로 롤백 워프.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr)
		return true;

	Player->OnServerMoveRejected(pkt.last_accepted_seq(), pkt.tile_x(), pkt.tile_y());
	ScreenLog(L"[Client] S_MOVE_REJECT accepted<=%llu stay=(%d,%d)",
		pkt.last_accepted_seq(), pkt.tile_x(), pkt.tile_y());
	return true;
}

bool Handle_S_MONSTER_SPAWN(PacketSessionRef& /*session*/, Protocol::S_MONSTER_SPAWN& pkt)
{
	// 서버가 스폰한 몬스터를 월드에 생성한다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	World->SpawnActor<AMonsterActor>(pkt.monster_id(), pkt.tile_x(), pkt.tile_y());
	ScreenLog(L"[Client] S_MONSTER_SPAWN id=%llu tile=(%d,%d)", pkt.monster_id(), pkt.tile_x(), pkt.tile_y());
	return true;
}

bool Handle_S_MONSTER_MOVE(PacketSessionRef& /*session*/, Protocol::S_MONSTER_MOVE& pkt)
{
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Monster = FindMonsterActor(World, pkt.monster_id());
	if (Monster == nullptr) return true;

	Monster->OnServerMove(pkt.tile_x(), pkt.tile_y());
	return true;
}

bool Handle_S_MONSTER_ATTACK(PacketSessionRef& /*session*/, Protocol::S_MONSTER_ATTACK& pkt)
{
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Monster = FindMonsterActor(World, pkt.monster_id());
	if (Monster == nullptr) return true;

	Monster->OnServerAttack();
	ScreenLog(L"[Client] S_MONSTER_ATTACK monster=%llu -> player=%llu", pkt.monster_id(), pkt.target_player_id());
	return true;
}

bool Handle_S_PLAYER_DAMAGED(PacketSessionRef& /*session*/, Protocol::S_PLAYER_DAMAGED& pkt)
{
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr) return true;

	Player->OnServerDamaged(pkt.hp(), pkt.max_hp());
	ScreenLog(L"[Client] S_PLAYER_DAMAGED id=%llu hp=%d/%d", pkt.player_id(), pkt.hp(), pkt.max_hp());
	return true;
}

bool Handle_S_PLAYER_DIED(PacketSessionRef& /*session*/, Protocol::S_PLAYER_DIED& pkt)
{
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr) return true;

	Player->OnServerDied();
	ScreenLog(L"[Client] S_PLAYER_DIED id=%llu", pkt.player_id());
	return true;
}

bool Handle_S_MONSTER_DAMAGED(PacketSessionRef& /*session*/, Protocol::S_MONSTER_DAMAGED& pkt)
{
	// 몬스터는 체력바 UI 가 없지만, ACharacterActor::OnServerDamaged 훅을 타야 피격 이펙트가 스폰된다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Monster = FindMonsterActor(World, pkt.monster_id());
	if (Monster == nullptr) return true;

	Monster->OnServerDamaged(pkt.hp(), pkt.max_hp());
	ScreenLog(L"[Client] S_MONSTER_DAMAGED id=%llu hp=%d/%d", pkt.monster_id(), pkt.hp(), pkt.max_hp());
	return true;
}

bool Handle_S_MONSTER_DIED(PacketSessionRef& /*session*/, Protocol::S_MONSTER_DIED& pkt)
{
	// 서버가 권위적으로 사망을 통보 — 월드에서 즉시 제거(증발).
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	auto Monster = FindMonsterActor(World, pkt.monster_id());
	if (Monster == nullptr) return true;

	World->DestroyActor(Monster);
	ScreenLog(L"[Client] S_MONSTER_DIED id=%llu", pkt.monster_id());
	return true;
}