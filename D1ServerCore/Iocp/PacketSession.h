#pragma once

#include <memory>
#include "Session.h"

/**
 * PacketHeader 기반 프레이밍을 수행하는 Session 파생 클래스.
 *
 * TCP 는 스트림 프로토콜이므로 한 번의 Recv 가 여러 패킷으로 병합되어 오거나
 * 반대로 하나의 패킷이 여러 번에 나누어 도착할 수 있다.
 * PacketSession::OnRecv 는 RecvBuffer 에 누적된 바이트를 스캔하면서
 * 완전한 패킷 단위로만 OnRecvPacket 가상함수를 호출한다.
 *
 * 서브클래스(ClientSession / ServerSession)는 OnRecvPacket 안에서
 * 각자의 PacketHandler 테이블로 디스패치한다.
 *
 * OnRecv 는 final 로 잠겨 있으므로 서브클래스에서 재오버라이드할 수 없다.
 */
class PacketSession : public Session
{
public:
	/** 이 세션의 shared_ptr (PacketSession 타입으로 다운캐스트)을 반환한다. */
	PacketSessionRef GetPacketSessionRef();

protected:
	/**
	 * 한 건의 완전한 패킷이 조립되었을 때 호출된다.
	 *
	 * @param Buffer  패킷 시작 포인터 (PacketHeader 포함)
	 * @param Len     전체 패킷 바이트 수 (= PacketHeader::Size)
	 */
	virtual void OnRecvPacket(BYTE* Buffer, int32 Len) = 0;

private:
	/**
	 * PacketHeader 기반 파싱 루프.
	 * - 헤더 미도착      → 소비 바이트 0 반환, 다음 Recv 대기
	 * - 본문 미도착      → 지금까지 처리한 바이트만 반환
	 * - 단일/복수 패킷   → 모두 순차 디스패치 후 누적 소비 바이트 반환
	 */
	int32 OnRecv(uint8* Data, int32 NumOfBytes) final;
};
