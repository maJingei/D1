#include "GameServerSession.h"

#include "ClientPacketHandler.h"
#include "GameRoom.h"

namespace D1
{
	void GameServerSession::OnRecvPacket(BYTE* Buffer, int32 Len)
	{
		PacketSessionRef Ref = GetPacketSessionRef();
		ClientPacketHandler::HandlePacket(Ref, Buffer, Len);
	}

	void GameServerSession::OnDisconnected()
	{
		// 입장 이후 끊긴 경우에만 Room 에서 제거한다. 입장 전이면 PlayerID=0 이라 Leave 는 no-op.
		if (PlayerID != 0)
			GameRoom::Get()->Leave(PlayerID);
	}
}