#pragma once

#include "Core/CoreMinimal.h"

/** 모든 패킷 앞에 붙는 4바이트 고정 헤더. */
#pragma pack(push, 1)
struct PacketHeader
{
	uint16 Size; // 헤더 + 페이로드 총 바이트 (최대 UINT16_MAX)
	uint16 Id;   // 프로토콜 ID (PacketID enum 값)
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be exactly 4 bytes on the wire.");
