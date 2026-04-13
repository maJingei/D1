#include "ServerPacketHandler.h"
#include <iostream>

using namespace D1;

PacketHandlerFunc GPacketHandler[UINT16_MAX];

bool Handle_INVALID(PacketSessionRef& /*session*/, BYTE* buffer, int32 /*len*/)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	std::cout << "[Client] Invalid packet id=" << header->Id << std::endl;
	return false;
}

bool Handle_S_LOGIN(PacketSessionRef& /*session*/, Protocol::S_LOGIN& pkt)
{
	if (pkt.result() == 0)
		std::cout << "[Client] LOGIN success: userId=" << pkt.user_id() << std::endl;
	else
		std::cout << "[Client] LOGIN failed: result=" << pkt.result() << std::endl;
	return true;
}