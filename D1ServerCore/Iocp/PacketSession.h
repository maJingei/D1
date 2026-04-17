#pragma once

#include <memory>
#include "Session.h"

/** PacketHeader 기반 프레이밍을 수행하는 Session 파생 클래스. */
class PacketSession : public Session
{
public:
	/** 이 세션의 shared_ptr (PacketSession 타입으로 다운캐스트)을 반환한다. */
	PacketSessionRef GetPacketSessionRef();

protected:
	/** 한 건의 완전한 패킷이 조립되었을 때 호출된다. */
	virtual void OnRecvPacket(BYTE* Buffer, int32 Len) = 0;

private:
	/** PacketHeader 기반 파싱 루프. */
	int32 OnRecv(uint8* Data, int32 NumOfBytes) final;
};
