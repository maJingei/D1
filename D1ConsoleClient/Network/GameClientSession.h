#pragma once

#include "Iocp/PacketSession.h"

#include <string>

/** D1ConsoleClient 전용 Session 파생. */
class GameClientSession : public PacketSession
{
public:
	/**
	 * 지정된 ID/PW 로 C_LOGIN 패킷을 전송한다.
	 * ULoginWidget 이 [Login] 버튼/Enter 제출 경로에서 호출한다. OnConnected 의 env 경로도 내부적으로 본 함수를 호출.
	 */
	void SendLoginPacket(const std::string& Id, const std::string& Pw);

protected:
	void OnConnected() override;
	void OnRecvPacket(BYTE* Buffer, int32 Len) override;
	void OnSend(int32 NumOfBytes) override;
};
