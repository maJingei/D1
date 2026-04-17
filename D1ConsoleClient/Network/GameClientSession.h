#pragma once

#include "Iocp/PacketSession.h"

/** D1ConsoleClient 전용 Session 파생. */
class GameClientSession : public PacketSession
{
protected:
	void OnConnected() override;
	void OnRecvPacket(BYTE* Buffer, int32 Len) override;
	void OnSend(int32 NumOfBytes) override;

private:
	/** 필드 입장 요청을 서버로 전송한다. 서버가 PlayerID/스폰 좌표를 부여해 S_ENTER_GAME 으로 응답. */
	void SendEnterGamePacket();
};
