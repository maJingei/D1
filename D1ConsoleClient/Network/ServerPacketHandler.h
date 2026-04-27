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
	PKT_C_CHAT = 1019,
	PKT_S_CHAT = 1020,
	PKT_C_DEBUG_FORCE_REJECT = 1021,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len);
bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt);
bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt);
bool Handle_S_SPAWN(PacketSessionRef& session, Protocol::S_SPAWN& pkt);
bool Handle_S_MOVE(PacketSessionRef& session, Protocol::S_MOVE& pkt);
bool Handle_S_MOVE_REJECT(PacketSessionRef& session, Protocol::S_MOVE_REJECT& pkt);
bool Handle_S_MONSTER_SPAWN(PacketSessionRef& session, Protocol::S_MONSTER_SPAWN& pkt);
bool Handle_S_MONSTER_MOVE(PacketSessionRef& session, Protocol::S_MONSTER_MOVE& pkt);
bool Handle_S_MONSTER_ATTACK(PacketSessionRef& session, Protocol::S_MONSTER_ATTACK& pkt);
bool Handle_S_PLAYER_DAMAGED(PacketSessionRef& session, Protocol::S_PLAYER_DAMAGED& pkt);
bool Handle_S_PLAYER_DIED(PacketSessionRef& session, Protocol::S_PLAYER_DIED& pkt);
bool Handle_S_MONSTER_DAMAGED(PacketSessionRef& session, Protocol::S_MONSTER_DAMAGED& pkt);
bool Handle_S_MONSTER_DIED(PacketSessionRef& session, Protocol::S_MONSTER_DIED& pkt);
bool Handle_S_PORTAL_TELEPORT(PacketSessionRef& session, Protocol::S_PORTAL_TELEPORT& pkt);
bool Handle_S_PLAYER_LEFT(PacketSessionRef& session, Protocol::S_PLAYER_LEFT& pkt);
bool Handle_S_PLAYER_ATTACK(PacketSessionRef& session, Protocol::S_PLAYER_ATTACK& pkt);
bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt);

class ServerPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[PKT_S_LOGIN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_LOGIN>(Handle_S_LOGIN, session, buffer, len); };
		GPacketHandler[PKT_S_ENTER_GAME] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_ENTER_GAME>(Handle_S_ENTER_GAME, session, buffer, len); };
		GPacketHandler[PKT_S_SPAWN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_SPAWN>(Handle_S_SPAWN, session, buffer, len); };
		GPacketHandler[PKT_S_MOVE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MOVE>(Handle_S_MOVE, session, buffer, len); };
		GPacketHandler[PKT_S_MOVE_REJECT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MOVE_REJECT>(Handle_S_MOVE_REJECT, session, buffer, len); };
		GPacketHandler[PKT_S_MONSTER_SPAWN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MONSTER_SPAWN>(Handle_S_MONSTER_SPAWN, session, buffer, len); };
		GPacketHandler[PKT_S_MONSTER_MOVE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MONSTER_MOVE>(Handle_S_MONSTER_MOVE, session, buffer, len); };
		GPacketHandler[PKT_S_MONSTER_ATTACK] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MONSTER_ATTACK>(Handle_S_MONSTER_ATTACK, session, buffer, len); };
		GPacketHandler[PKT_S_PLAYER_DAMAGED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PLAYER_DAMAGED>(Handle_S_PLAYER_DAMAGED, session, buffer, len); };
		GPacketHandler[PKT_S_PLAYER_DIED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PLAYER_DIED>(Handle_S_PLAYER_DIED, session, buffer, len); };
		GPacketHandler[PKT_S_MONSTER_DAMAGED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MONSTER_DAMAGED>(Handle_S_MONSTER_DAMAGED, session, buffer, len); };
		GPacketHandler[PKT_S_MONSTER_DIED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MONSTER_DIED>(Handle_S_MONSTER_DIED, session, buffer, len); };
		GPacketHandler[PKT_S_PORTAL_TELEPORT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PORTAL_TELEPORT>(Handle_S_PORTAL_TELEPORT, session, buffer, len); };
		GPacketHandler[PKT_S_PLAYER_LEFT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PLAYER_LEFT>(Handle_S_PLAYER_LEFT, session, buffer, len); };
		GPacketHandler[PKT_S_PLAYER_ATTACK] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PLAYER_ATTACK>(Handle_S_PLAYER_ATTACK, session, buffer, len); };
		GPacketHandler[PKT_S_CHAT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_CHAT>(Handle_S_CHAT, session, buffer, len); };
	}

	static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		return GPacketHandler[header->Id](session, buffer, len);
	}
	static SendBufferRef MakeSendBuffer(Protocol::C_LOGIN& pkt) { return MakeSendBuffer(pkt, PKT_C_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, PKT_C_ENTER_GAME); }
	static SendBufferRef MakeSendBuffer(Protocol::C_MOVE& pkt) { return MakeSendBuffer(pkt, PKT_C_MOVE); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ATTACK& pkt) { return MakeSendBuffer(pkt, PKT_C_ATTACK); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CHAT& pkt) { return MakeSendBuffer(pkt, PKT_C_CHAT); }
	static SendBufferRef MakeSendBuffer(Protocol::C_DEBUG_FORCE_REJECT& pkt) { return MakeSendBuffer(pkt, PKT_C_DEBUG_FORCE_REJECT); }

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