#pragma once

#include "../Core/Types.h"

namespace D1
{
	/**
	 * 모든 패킷 앞에 붙는 4바이트 고정 헤더.
	 *
	 * Size 필드는 "헤더(4) + 페이로드" 의 총 바이트 수를 가리킨다.
	 * 따라서 페이로드 크기는 Size - sizeof(PacketHeader) 로 계산한다.
	 *
	 * uint16 상한으로 최대 패킷 크기는 65535 바이트.
	 * 네트워크 와이어 포맷이므로 반드시 1바이트 정렬(packing 1)을 강제한다.
	 */
	#pragma pack(push, 1)
	struct PacketHeader
	{
		uint16 Size; // 헤더 + 페이로드 총 바이트 (최대 UINT16_MAX)
		uint16 Id;   // 프로토콜 ID (PacketID enum 값)
	};
	#pragma pack(pop)

	static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be exactly 4 bytes on the wire.");
}