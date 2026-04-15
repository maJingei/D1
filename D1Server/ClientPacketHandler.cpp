#include "ClientPacketHandler.h"
#include "GameRoom.h"
#include "GameServerSession.h"
#include "MoveCounter.h"
#include <iostream>

using namespace D1;

PacketHandlerFunc GPacketHandler[UINT16_MAX];

// 서버 측 부하 벤치마크용 C_MOVE 카운터.
// D1Server.cpp 의 TPS 로거가 1초 주기로 이 값을 스냅샷/diff 로 읽는다.
// relaxed 로 증가해도 문제없음 — 정확한 실시간 값이 아니라 누적 카운트 delta 가 목적.
std::atomic<uint64> GMoveCounter{ 0 };

bool Handle_INVALID(PacketSessionRef& /*session*/, BYTE* buffer, int32 /*len*/)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	std::cout << "[Server] Invalid packet id=" << header->Id << "\n";
	return false;
}

bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	std::cout << "[Server] C_LOGIN: id=" << pkt.id() << ", pw=" << pkt.pw() << "\n";

	Protocol::S_LOGIN res;
	res.set_result(0);
	res.set_user_id(1);

	session->Send(ClientPacketHandler::MakeSendBuffer(res));
	return true;
}

bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt)
{
	// 부하 벤치마크용 수신 카운터. 진입부에 한 번만 증가시키고, 이후 경로는 그대로 유지한다.
	GMoveCounter.fetch_add(1, std::memory_order_relaxed);

	// 권위적 이동 검증은 GameRoom 에 위임 — 세션을 PlayerID 로 식별한 뒤 TryMove 내부에서 점유/차단 검사 + broadcast/reject 처리.
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
		return false;

	GameRoom::Get()->TryMove(PlayerID, pkt.dir());
	return true;
}

bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& /*pkt*/)
{
	// 1. PacketSession 참조를 GameServerSession 으로 캐스팅. 본 프로젝트의 서버는 해당 타입만 수용하므로 안전.
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);

	// 2. Room 에 입장 처리 — PlayerID/스폰 좌표 발급 + 기존 플레이어 스냅샷 수신
	int32 SpawnTileX = 0;
	int32 SpawnTileY = 0;
	std::vector<GameRoom::PlayerEntry> Others;
	const uint64 NewPlayerID = GameRoom::Get()->Enter(GameSession, SpawnTileX, SpawnTileY, Others);

	// 3. Session 에 PlayerID 기록 — 이후 disconnect 시 Leave 에 사용된다
	GameSession->SetPlayerID(NewPlayerID);

	std::cout << "[Server] C_ENTER_GAME: newId=" << NewPlayerID
		<< " spawn=(" << SpawnTileX << "," << SpawnTileY << ")"
		<< " others=" << Others.size() << "\n";

	// 4. 신규 입장자에게 S_ENTER_GAME 응답 — 본인 ID/좌표 + 기존 플레이어 스냅샷 포함
	Protocol::S_ENTER_GAME EnterRes;
	EnterRes.set_player_id(NewPlayerID);
	EnterRes.set_tile_x(SpawnTileX);
	EnterRes.set_tile_y(SpawnTileY);
	for (const auto& Entry : Others)
	{
		Protocol::PlayerInfo* Info = EnterRes.add_others();
		Info->set_player_id(Entry.PlayerID);
		Info->set_tile_x(Entry.TileX);
		Info->set_tile_y(Entry.TileY);
	}
	GameSession->Send(ClientPacketHandler::MakeSendBuffer(EnterRes));

	// 5. 기존 접속자들에게 S_SPAWN 브로드캐스트 — 본인 제외
	Protocol::S_SPAWN SpawnPkt;
	SpawnPkt.set_player_id(NewPlayerID);
	SpawnPkt.set_tile_x(SpawnTileX);
	SpawnPkt.set_tile_y(SpawnTileY);
	GameRoom::Get()->Broadcast(ClientPacketHandler::MakeSendBuffer(SpawnPkt), NewPlayerID);

	return true;
}