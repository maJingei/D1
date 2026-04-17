#include "PacketSession.h"
#include "PacketHeader.h"

PacketSessionRef PacketSession::GetPacketSessionRef()
{
	// IocpObject 가 enable_shared_from_this<IocpObject> 를 제공하므로
	// shared_from_this() 는 shared_ptr<IocpObject> 로 반환된다.
	// PacketSession 타입으로 다운캐스트.
	return std::static_pointer_cast<PacketSession>(shared_from_this());
}

int32 PacketSession::OnRecv(uint8* Data, int32 NumOfBytes)
{
	int32 ProcessLen = 0;

	// 한 번의 Recv 에서 받은 누적 바이트를 가능한 만큼 연속 파싱한다.
	// 미완성 패킷이 남으면 RecvBuffer 에 보관되어 다음 Recv 때 이어 처리된다.
	while (true)
	{
		const int32 DataSize = NumOfBytes - ProcessLen;

		// (1) 헤더조차 도착하지 않음 → 다음 수신 대기
		if (DataSize < static_cast<int32>(sizeof(PacketHeader)))
			break;

		PacketHeader* Header = reinterpret_cast<PacketHeader*>(&Data[ProcessLen]);

		// (2) 헤더는 받았지만 본문 일부가 미도착 → 이번 라운드 종료
		if (DataSize < static_cast<int32>(Header->Size))
			break;

		// (3)/(4) 완전한 패킷 1건 조립 완료 → 서브클래스에 디스패치
		OnRecvPacket(reinterpret_cast<BYTE*>(&Data[ProcessLen]), static_cast<int32>(Header->Size));
		ProcessLen += Header->Size;
	}

	return ProcessLen;
}
