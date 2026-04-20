#include "Network/GameServerSession.h"

#include "Network/ClientPacketHandler.h"
#include "World/World.h"
#include "World/Level.h"

void GameServerSession::OnRecvPacket(BYTE* Buffer, int32 Len)
{
	PacketSessionRef Ref = GetPacketSessionRef();
	ClientPacketHandler::HandlePacket(Ref, Buffer, Len);
}

void GameServerSession::OnDisconnected()
{
	if (PlayerID != 0 && LevelID >= 0)
		World::GetInstance().GetLevel(LevelID)->DoAsync(&Level::DoLeave, PlayerID);
}