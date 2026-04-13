#include "ClientPacketHandler.h"
#include <iostream>

using namespace D1;

PacketHandlerFunc GPacketHandler[UINT16_MAX];

bool Handle_INVALID(PacketSessionRef& /*session*/, BYTE* buffer, int32 /*len*/)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	std::cout << "[Server] Invalid packet id=" << header->Id << std::endl;
	return false;
}

bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	std::cout << "[Server] C_LOGIN: id=" << pkt.id() << ", pw=" << pkt.pw() << std::endl;

	Protocol::S_LOGIN res;
	res.set_result(0);
	res.set_user_id(1);

	session->Send(ClientPacketHandler::MakeSendBuffer(res));
	return true;
}