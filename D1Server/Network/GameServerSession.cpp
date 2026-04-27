#include "Network/GameServerSession.h"

#include "Network/ClientPacketHandler.h"
#include "World/World.h"
#include "World/Level.h"

// 외부 선언 — 정의는 ClientPacketHandler.cpp. (헤더에 선언이 자동으로 사라지는 환경 대응)
const char* GetPacketName(uint16 PacketId);

void GameServerSession::OnRecvPacket(BYTE* Buffer, int32 Len)
{
	// 봇 테스트 가시성 — 수신 패킷의 Level/Player/이름을 1줄 로그.
	PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
	const char* PacketName = GetPacketName(Header->Id);
	if (PacketName != nullptr)
	{
		std::cout << "[Packet][Level " << LevelID << "] RECV " << PacketName << " player=" << PlayerID << "\n";
	}
	else
	{
		std::cout << "[Packet][Level " << LevelID << "] RECV id=" << Header->Id << " player=" << PlayerID << "\n";
	}

	PacketSessionRef Ref = GetPacketSessionRef();
	ClientPacketHandler::HandlePacket(Ref, Buffer, Len);
}

void GameServerSession::Send(SendBufferRef InSendBuffer)
{
	// 봇 테스트 가시성 — 송신 패킷의 Level/Player/이름을 1줄 로그. base Send 가 nullptr 처리하므로 여기선 헤더 접근 전 nullptr 가드 필요.
	if (InSendBuffer != nullptr)
	{
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(InSendBuffer->Buffer());
		const char* PacketName = GetPacketName(Header->Id);
		if (PacketName != nullptr)
		{
			std::cout << "[Packet][Level " << LevelID << "] SEND " << PacketName << " player=" << PlayerID << "\n";
		}
		else
		{
			std::cout << "[Packet][Level " << LevelID << "] SEND id=" << Header->Id << " player=" << PlayerID << "\n";
		}
	}

	Session::Send(std::move(InSendBuffer));
}

void GameServerSession::OnDisconnected()
{
	if (PlayerID == 0 || LevelID < 0)
		return;

	auto TargetLevel = World::GetInstance().GetLevel(LevelID);
	// 실유저: Playing 상태 + AccountId 존재 → PlayerEntry save + AccountMap 제거.
	// 봇(is_bot): AccountId 비어있으므로 save 건너뛰고 단순 Leave.
	if (GetState() == ESessionState::Playing && AccountId.empty() == false)
	{
		TargetLevel->DoAsync(&Level::DoLogoutAndSave, PlayerID, AccountId);
		SetState(ESessionState::NotLoggedIn);
	}
	else
	{
		TargetLevel->DoAsync(&Level::DoLeave, PlayerID);
	}
}