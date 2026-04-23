#include "ServerPacketHandler.h"
#include "Game.h"
#include "World/UWorld.h"
#include "GameObject/APlayerActor.h"
#include "GameObject/APlayerFemaleActor.h"
#include "GameObject/APlayerDwarfActor.h"
#include "GameObject/AMonsterActor.h"
#include "UI/ULoginWidget.h"

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

	/** World 내 APlayerActor 중 지정 PlayerID 를 가진 액터를 찾는다. 없으면 nullptr. UWorld 의 ID 맵으로 위임 — O(1) 조회. */
	std::shared_ptr<APlayerActor> FindPlayerActor(UWorld* World, uint64 PlayerID)
	{
		if (World == nullptr) return nullptr;
		return World->FindPlayerActor(PlayerID);
	}

	/** World 내 AMonsterActor 중 지정 MonsterID 를 가진 액터를 찾는다. 없으면 nullptr. UWorld 의 ID 맵으로 위임 — O(1) 조회. */
	std::shared_ptr<AMonsterActor> FindMonsterActor(UWorld* World, uint64 MonsterID)
	{
		if (World == nullptr) return nullptr;
		return World->FindMonsterActor(MonsterID);
	}

	/**
	 * CharacterType 에 따라 적절한 APlayerActor 파생 클래스를 스폰한다. 반환은 공통 베이스 포인터.
	 * S_ENTER_GAME(본인/기존 접속자) / S_SPAWN 공용으로 사용되어 캐릭터 타입 분기 코드 중복을 방지한다.
	 */
	std::shared_ptr<APlayerActor> SpawnPlayerByType(UWorld* World, uint64 PlayerID, int32 TileX, int32 TileY, Protocol::CharacterType Type)
	{
		if (World == nullptr) return nullptr;
		switch (Type)
		{
		case Protocol::CT_FEMALE: return World->SpawnActor<APlayerFemaleActor>(PlayerID, TileX, TileY);
		case Protocol::CT_DWARF:  return World->SpawnActor<APlayerDwarfActor>(PlayerID, TileX, TileY);
		case Protocol::CT_DEFAULT:
		default:                  return World->SpawnActor<APlayerActor>(PlayerID, TileX, TileY);
		}
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
	// ELoginResult 분기 — 성공 시 Scene 전환, 실패 시 LoginWidget 에 에러 메시지 표시 + 재시도 허용.
	Game* G = Game::GetInstance();
	ULoginWidget* Widget = (G != nullptr) ? G->GetLoginWidget() : nullptr;

	switch (pkt.result())
	{
	case Protocol::LR_SUCCESS:
		ScreenLog(L"[Client] LOGIN success: userId=%llu", pkt.user_id());
		if (G != nullptr)
			G->SetCurrentState(EGameState::InGame);
		if (Widget != nullptr)
			Widget->SetVisible(false);
		break;

	case Protocol::LR_INVALID_CREDENTIALS:
		ScreenLog(L"[Client] LOGIN failed: invalid credentials");
		if (Widget != nullptr)
			Widget->ShowError(L"ID 또는 비밀번호가 올바르지 않습니다.");
		break;

	case Protocol::LR_ALREADY_LOGGED_IN:
		ScreenLog(L"[Client] LOGIN failed: already logged in");
		if (Widget != nullptr)
			Widget->ShowError(L"이미 로그인된 계정입니다.");
		break;

	case Protocol::LR_INVALID_REQUEST_FORMAT:
		ScreenLog(L"[Client] LOGIN failed: invalid request format");
		if (Widget != nullptr)
			Widget->ShowError(L"ID/비밀번호 형식이 잘못되었습니다.");
		break;

	case Protocol::LR_DB_ERROR:
		ScreenLog(L"[Client] LOGIN failed: DB error");
		if (Widget != nullptr)
			Widget->ShowError(L"서버 오류가 발생했습니다.");
		break;

	default:
		ScreenLog(L"[Client] LOGIN failed: result=%u", pkt.result());
		if (Widget != nullptr)
			Widget->ShowError(L"알 수 없는 로그인 오류입니다.");
		break;
	}
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
	// 서버가 배정한 Level 로 렌더/충돌 기준을 전환. 초기 진입과 포탈 후속 S_ENTER_GAME 양쪽을 커버한다.
	World->SetCurrentLevelID(static_cast<int32>(pkt.room_id()));
	
	// 서버가 이 세션에 배정한 CharacterType 로 파생 Actor 스폰 — 타입별 스프라이트 시트 자동 적용.
	std::shared_ptr<APlayerActor> MyPlayer = SpawnPlayerByType(World, MyPlayerID, MyTileX, MyTileY, pkt.character_type());
	// 서버 기준 TileMoveSpeed 를 본인 액터에 주입 — 로컬 입력 쿨다운이 서버와 동일해지도록.
	if (MyPlayer != nullptr)
		MyPlayer->SetTileMoveSpeed(pkt.move_speed());

	ScreenLog(L"[Client] S_ENTER_GAME myId=%llu tile=(%d,%d) others=%d speed=%.2f type=%d",
		MyPlayerID, MyTileX, MyTileY, pkt.others_size(), pkt.move_speed(), static_cast<int>(pkt.character_type()));

	// 2. 기존 접속자 스냅샷 — 신규 입장 시점에 이미 필드에 있던 플레이어들을 스폰.
	//    각 PlayerInfo 의 character_type 을 그대로 사용해 타인도 올바른 스프라이트로 보인다.
	for (int32 i = 0; i < pkt.others_size(); ++i)
	{
		const Protocol::PlayerInfo& Info = pkt.others(i);
		SpawnPlayerByType(World, Info.player_id(), Info.tile_x(), Info.tile_y(), Info.character_type());
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

	ScreenLog(L"[Client] S_SPAWN id=%llu tile=(%d,%d) type=%d",	pkt.player_id(), pkt.tile_x(), pkt.tile_y(), static_cast<int>(pkt.character_type()));

	SpawnPlayerByType(World, pkt.player_id(), pkt.tile_x(), pkt.tile_y(), pkt.character_type());
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

	Player->OnServerMoveRejected(pkt.client_seq(), pkt.last_accepted_seq(), pkt.tile_x(), pkt.tile_y());
	ScreenLog(L"[Client] S_MOVE_REJECT rejected=%llu accepted<=%llu stay=(%d,%d)",
		pkt.client_seq(), pkt.last_accepted_seq(), pkt.tile_x(), pkt.tile_y());
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
	// 사망 = 월드 액터 제거(본인/타인 동일). 본인은 관전 뷰 전환, 타 클라는 죽은 캐릭터가 증발.
	// 서버의 Players 맵에는 세션이 남아있어 이후에도 몬스터/플레이어 이동 패킷을 계속 수신한다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr) return true;

	World->DestroyActor(Player);
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

bool Handle_S_PORTAL_TELEPORT(PacketSessionRef& /*session*/, Protocol::S_PORTAL_TELEPORT& pkt)
{
	// 서버가 Portal 을 통해 다른 Level 로 전이시켰음을 통보. 현재 월드의 Actor 들을 모두 제거하고
	// 후속 S_ENTER_GAME 을 대기한다. S_ENTER_GAME 이 본인/기존 플레이어/몬스터를 재스폰한다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	// 맵 이동 후 스냅샷 모두 정리
	if (auto MyPlayer = FindPlayerActor(World, Instance->GetMyPlayerID()))
		MyPlayer->ClearSnapshotQueue();

	// DestroyActor 는 Actors 벡터를 수정하므로 순회 중 제거 불가. 먼저 스냅샷을 떠 순회 후 일괄 제거한다.
	std::vector<std::shared_ptr<AActor>> Snapshot = World->GetActorsForIteration();
	for (const std::shared_ptr<AActor>& Actor : Snapshot)
		World->DestroyActor(Actor);

	// 타일 레이어/충돌 맵을 새 Level 로 스위칭. UWorld 는 LEVEL_COUNT 개 세트를 프리로드한 상태이므로 디스크 재로드는 없다.
	World->SetCurrentLevelID(static_cast<int32>(pkt.new_level_id()));

	ScreenLog(L"[Client] S_PORTAL_TELEPORT new_level=%u spawn=(%d,%d)",
		pkt.new_level_id(), pkt.spawn_tile_x(), pkt.spawn_tile_y());
	return true;
}

bool Handle_S_PLAYER_LEFT(PacketSessionRef& /*session*/, Protocol::S_PLAYER_LEFT& pkt)
{
	// 같은 Level 에 있던 다른 플레이어가 퇴장/Portal 이동으로 사라졌음을 알린다. 해당 Actor 를 월드에서 제거.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();
	if (World == nullptr) return false;

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr) return true;

	World->DestroyActor(Player);
	ScreenLog(L"[Client] S_PLAYER_LEFT id=%llu", pkt.player_id());
	return true;
}

bool Handle_S_PLAYER_ATTACK(PacketSessionRef& /*session*/, Protocol::S_PLAYER_ATTACK& pkt)
{
	// 같은 Level 의 다른 플레이어가 공격 액션을 시작했음을 알린다. 공격자 본인은 서버에서 ExceptID 로 제외되므로 이 핸들러에 도달하지 않는다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr) return true;

	Player->OnServerAttack();
	ScreenLog(L"[Client] S_PLAYER_ATTACK id=%llu", pkt.player_id());
	return true;
}