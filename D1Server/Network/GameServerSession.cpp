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
	if (PlayerID == 0 || LevelID < 0)
		return;

	auto TargetLevel = World::GetInstance().GetLevel(LevelID);
	// 실유저: Playing 상태 + AccountId 존재 → PlayerEntry save + AccountMap 제거.
	// 봇(is_bot): AccountId 비어있으므로 save 건너뛰고 단순 Leave.
	if (GetState() == ESessionState::Playing && AccountId.empty() == false)
	{
		TargetLevel->DoAsync(&Level::DoLogoutAndSave, PlayerID, AccountId);
		SetState(ESessionState::NotLoggedIn);
	}
	else
	{
		TargetLevel->DoAsync(&Level::DoLeave, PlayerID);
	}
}