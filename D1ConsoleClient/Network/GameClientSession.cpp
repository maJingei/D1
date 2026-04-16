#include "GameClientSession.h"

#include "Iocp/Session.h"
#include "Iocp/PacketSession.h"

#include "Network/ServerPacketHandler.h"

void GameClientSession::OnConnected()
{
	// 베이스가 RegisterRecv 를 돌려주므로 먼저 수신 대기를 시작시킨 뒤, 바로 EnterGame 요청 패킷을 올려 보낸다.
	Session::OnConnected();
	SendEnterGamePacket();
}

void GameClientSession::OnRecvPacket(BYTE* Buffer, int32 Len)
{
	PacketSessionRef Ref = GetPacketSessionRef();
	ServerPacketHandler::HandlePacket(Ref, Buffer, Len);
}

void GameClientSession::OnSend(int32 /*NumOfBytes*/)
{
	// 클라이언트에서는 송신 완료 로그를 생략한다.
}

void GameClientSession::SendEnterGamePacket()
{
	// C_ENTER_GAME 은 현 스코프에서 빈 payload. 서버가 Session 기반으로 PlayerID/스폰 좌표를 발급한다.
	Protocol::C_ENTER_GAME pkt;
	Send(ServerPacketHandler::MakeSendBuffer(pkt));
}
