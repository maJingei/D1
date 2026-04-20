#include "Network/ClientPacketHandler.h"
#include "World/World.h"
#include "World/Level.h"
#include "Network/GameServerSession.h"
#include <iostream>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

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
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
		return false;

	const int32 LevelID = GameSession->GetLevelID();
	if (LevelID < 0)
		return true;

	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoTryMove, PlayerID, pkt.dir(), pkt.client_seq());
	return true;
}

bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& /*pkt*/)
{
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	World::GetInstance().EnterAnyLevel(GameSession);
	return true;
}

bool Handle_C_ATTACK(PacketSessionRef& session, Protocol::C_ATTACK& /*pkt*/)
{
	// SpaceBar 입력 통보 — 서버가 발신 PlayerID + LastDir 로 1칸 앞 몬스터를 권위적으로 검색해 데미지를 적용한다.
	auto GameSession = std::static_pointer_cast<GameServerSession>(session);
	const uint64 PlayerID = GameSession->GetPlayerID();
	if (PlayerID == 0)
		return false;
	
	const int32 LevelID = GameSession->GetLevelID();
	if (LevelID < 0)
		return true;

	World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoTryAttack, PlayerID);
	return true;
}