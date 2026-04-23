#include "GameClientSession.h"
#include "Iocp/Session.h"
#include "Iocp/PacketSession.h"
#include "Network/ServerPacketHandler.h"
#include <windows.h>

void GameClientSession::OnConnected()
{
	// 접속 완료 후 env 가 둘 다 세팅된 경우에만 자동 C_LOGIN.
	// env 미설정 시에는 ULoginWidget 이 사용자 입력을 기다렸다가 SendLoginPacket(id, pw) 를 직접 호출한다.
	Session::OnConnected();

	char IdBuf[64] = {};
	char PwBuf[64] = {};
	const DWORD IdLen = ::GetEnvironmentVariableA("D1_LOGIN_ID", IdBuf, sizeof(IdBuf));
	const DWORD PwLen = ::GetEnvironmentVariableA("D1_LOGIN_PW", PwBuf, sizeof(PwBuf));
	if (IdLen == 0 || IdLen >= sizeof(IdBuf)) return;
	if (PwLen == 0 || PwLen >= sizeof(PwBuf)) return;

	SendLoginPacket(std::string(IdBuf), std::string(PwBuf));
}

void GameClientSession::OnRecvPacket(BYTE* Buffer, int32 Len)
{
	PacketSessionRef Ref = GetPacketSessionRef();
	ServerPacketHandler::HandlePacket(Ref, Buffer, Len);
}

void GameClientSession::OnSend(int32 /*NumOfBytes*/)
{
}

void GameClientSession::SendLoginPacket(const std::string& Id, const std::string& Pw)
{
	Protocol::C_LOGIN Pkt;
	Pkt.set_id(Id);
	Pkt.set_pw(Pw);
	Send(ServerPacketHandler::MakeSendBuffer(Pkt));
}