#pragma once
#include "Protocol.pb.h"
#include "Iocp/PacketSession.h"
#include "Iocp/PacketHeader.h"
#include "Iocp/SendBuffer.h"

using PacketHandlerFunc = std::function<bool(PacketSessionRef&, BYTE*, int32)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

enum : uint16
{
	PKT_C_LOGIN = 1000,
	PKT_S_LOGIN = 1001,
	PKT_C_ENTER_GAME = 1002,
	PKT_S_ENTER_GAME = 1003,
	PKT_S_SPAWN = 1004,
	PKT_C_MOVE = 1005,
	PKT_S_MOVE = 1006,
	PKT_S_MOVE_REJECT = 1007,
	PKT_S_MONSTER_SPAWN = 1008,
	PKT_S_MONSTER_MOVE = 1009,
	PKT_S_MONSTER_ATTACK = 1010,
	PKT_S_PLAYER_DAMAGED = 1011,
	PKT_S_PLAYER_DIED = 1012,
	PKT_C_ATTACK = 1013,
	PKT_S_MONSTER_DAMAGED = 1014,
	PKT_S_MONSTER_DIED = 1015,
	PKT_S_PORTAL_TELEPORT = 1016,
	PKT_S_PLAYER_LEFT = 1017,
	PKT_S_PLAYER_ATTACK = 1018,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len);
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt);
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt);
bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt);
bool Handle_C_ATTACK(PacketSessionRef& session, Protocol::C_ATTACK& pkt);

class ClientPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[PKT_C_LOGIN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, session, buffer, len); };
		GPacketHandler[PKT_C_ENTER_GAME] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_ENTER_GAME>(Handle_C_ENTER_GAME, session, buffer, len); };
		GPacketHandler[PKT_C_MOVE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_MOVE>(Handle_C_MOVE, session, buffer, len); };
		GPacketHandler[PKT_C_ATTACK] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_ATTACK>(Handle_C_ATTACK, session, buffer, len); };
	}

	static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		return GPacketHandler[header->Id](session, buffer, len);
	}
	static SendBufferRef MakeSendBuffer(Protocol::S_LOGIN& pkt) { return MakeSendBuffer(pkt, PKT_S_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, PKT_S_ENTER_GAME); }
	static SendBufferRef MakeSendBuffer(Protocol::S_SPAWN& pkt) { return MakeSendBuffer(pkt, PKT_S_SPAWN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MOVE& pkt) { return MakeSendBuffer(pkt, PKT_S_MOVE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MOVE_REJECT& pkt) { return MakeSendBuffer(pkt, PKT_S_MOVE_REJECT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MONSTER_SPAWN& pkt) { return MakeSendBuffer(pkt, PKT_S_MONSTER_SPAWN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MONSTER_MOVE& pkt) { return MakeSendBuffer(pkt, PKT_S_MONSTER_MOVE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MONSTER_ATTACK& pkt) { return MakeSendBuffer(pkt, PKT_S_MONSTER_ATTACK); }
	static SendBufferRef MakeSendBuffer(Protocol::S_PLAYER_DAMAGED& pkt) { return MakeSendBuffer(pkt, PKT_S_PLAYER_DAMAGED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_PLAYER_DIED& pkt) { return MakeSendBuffer(pkt, PKT_S_PLAYER_DIED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MONSTER_DAMAGED& pkt) { return MakeSendBuffer(pkt, PKT_S_MONSTER_DAMAGED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MONSTER_DIED& pkt) { return MakeSendBuffer(pkt, PKT_S_MONSTER_DIED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_PORTAL_TELEPORT& pkt) { return MakeSendBuffer(pkt, PKT_S_PORTAL_TELEPORT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_PLAYER_LEFT& pkt) { return MakeSendBuffer(pkt, PKT_S_PLAYER_LEFT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_PLAYER_ATTACK& pkt) { return MakeSendBuffer(pkt, PKT_S_PLAYER_ATTACK); }

private:
	template<typename PacketType, typename ProcessFunc>
	static bool HandlePacket(ProcessFunc func, PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketType pkt;
		if (pkt.ParseFromArray(buffer + sizeof(PacketHeader), len - sizeof(PacketHeader)) == false)
			return false;
		return func(session, pkt);
	}

	template<typename T>
	static SendBufferRef MakeSendBuffer(T& pkt, uint16 pktId)
	{
		const uint16 dataSize   = static_cast<uint16>(pkt.ByteSizeLong());
		const uint16 packetSize = dataSize + sizeof(PacketHeader);

		SendBufferRef sendBuffer = SendBufferManager::Get().Open(packetSize);
		PacketHeader* header     = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
		header->Size = packetSize;
		header->Id   = pktId;
		pkt.SerializeToArray(&header[1], dataSize);
		sendBuffer->Close(packetSize);

		return sendBuffer;
	}
};