#pragma once

#include "Iocp/PacketSession.h"

namespace D1
{
	/**
	 * D1ConsoleClient 전용 Session 파생. PacketSession 을 상속하여 PacketHeader 기반 프레이밍을 사용한다.
	 *
	 * - OnConnected : 접속 완료 즉시 C_ENTER_GAME 패킷을 자동 송신한다.
	 * - OnRecvPacket: 수신한 완전한 패킷 1건을 ServerPacketHandler 테이블로 디스패치한다.
	 */
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
}