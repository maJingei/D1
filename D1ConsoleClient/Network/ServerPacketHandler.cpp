#include "ServerPacketHandler.h"
#include "Game.h"
#include "World/UWorld.h"
#include "GameObject/APlayerActor.h"

#include <cstdio>
#include <cwchar>

using namespace D1;

namespace
{
	/**
	 * 화면 좌상단 오버레이로 한 줄을 찍는다. Game 인스턴스가 없으면 no-op.
	 * Windows subsystem 클라이언트에서는 std::cout 출력이 보이지 않으므로 이 경로가 유일한 UX 로그.
	 */
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
	// 서버가 확정한 이동. 자기/타인 공통 경로로 해당 액터의 TilePos 를 갱신하고 보간을 시작시킨다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr)
	{
		ScreenLog(L"[Client] S_MOVE for unknown player id=%llu", pkt.player_id());
		return true;
	}

	Player->OnServerMoveAccepted(pkt.tile_x(), pkt.tile_y(), pkt.dir());
	return true;
}

bool Handle_S_MOVE_REJECT(PacketSessionRef& /*session*/, Protocol::S_MOVE_REJECT& pkt)
{
	// 이동 거절 — 요청자에게만 수신되는 단일 전송. 입력 락 해제 + 현재 위치 동기화.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return false;
	UWorld* World = Instance->GetWorld();

	auto Player = FindPlayerActor(World, pkt.player_id());
	if (Player == nullptr)
		return true;

	Player->OnServerMoveRejected(pkt.tile_x(), pkt.tile_y());
	ScreenLog(L"[Client] S_MOVE_REJECT stay=(%d,%d)", pkt.tile_x(), pkt.tile_y());
	return true;
}