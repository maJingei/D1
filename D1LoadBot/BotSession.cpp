#include "BotSession.h"

#include "BotPacketIds.h"

#include "Iocp/PacketHeader.h"
#include "Iocp/SendBuffer.h"
#include "Protocol.pb.h"

#include <chrono>
#include <random>

namespace D1LoadBot
{
	namespace
	{
		/**
		 * Protobuf 메시지 + 패킷 ID 를 SendBuffer 로 직렬화한다.
		 *
		 * D1ConsoleClient/Network/ServerPacketHandler 의 템플릿 헬퍼와 동일한 와이어 포맷을 직접 구현한다.
		 * (Console 클라 헤더는 렌더/게임 상태에 강결합되어 있어 링크 제외한다.)
		 *
		 * @param Packet    직렬화할 Protobuf 메시지
		 * @param PacketId  와이어 헤더에 기록할 패킷 ID
		 * @return          전송 준비가 완료된 SendBuffer
		 */
		template<typename T>
		SendBufferRef MakeBotSendBuffer(T& Packet, uint16 PacketId)
		{
			const uint16 DataSize = static_cast<uint16>(Packet.ByteSizeLong());
			const uint16 PacketSize = DataSize + sizeof(PacketHeader);

			SendBufferRef Buffer = SendBufferManager::Get().Open(PacketSize);
			PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer->Buffer());
			Header->Size = PacketSize;
			Header->Id = PacketId;
			Packet.SerializeToArray(&Header[1], DataSize);
			Buffer->Close(PacketSize);

			return Buffer;
		}

		/**
		 * 스레드·봇별로 충분히 구별되는 난수 시드를 생성한다.
		 *
		 * steady_clock 나노초 값과 객체 포인터 주소를 XOR 해 봇마다 다른 시드를 보장한다.
		 *
		 * @param Salt  봇 인스턴스 포인터 (주소가 시드 엔트로피에 기여한다)
		 * @return      mt19937 초기화용 64비트 시드
		 */
		uint64 MakeSeed(void* Salt)
		{
			const auto TimePart = std::chrono::steady_clock::now().time_since_epoch().count();
			const auto PtrPart = reinterpret_cast<uintptr_t>(Salt);
			return static_cast<uint64>(TimePart) ^ static_cast<uint64>(PtrPart);
		}
	}

	BotSession::BotSession()
		: State(EBotState::Connecting)
		, RandomEngine(static_cast<std::mt19937::result_type>(MakeSeed(this)))
	{
	}

	void BotSession::OnConnected()
	{
		// 베이스 OnConnected 가 RegisterRecv 를 수행하므로 먼저 호출해 수신 큐를 켠다.
		Session::OnConnected();

		State.store(EBotState::LoggingIn, std::memory_order_relaxed);
		SendLogin();
	}

	void BotSession::OnDisconnected()
	{
		State.store(EBotState::Closing, std::memory_order_relaxed);
	}

	void BotSession::OnSend(int32 /*NumOfBytes*/)
	{
	}

	void BotSession::OnRecvPacket(BYTE* Buffer, int32 Len)
	{
		DispatchPacket(Buffer, Len);
	}

	void BotSession::RequestShutdown()
	{
		State.store(EBotState::Closing, std::memory_order_relaxed);
	}

	void BotSession::PumpMove()
	{
		if (State.load(std::memory_order_relaxed) != EBotState::InGame)
			return;

		const std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::now();

		// 첫 진입 직후에는 LastMoveSentAt 이 epoch 이므로 즉시 발사된다.
		const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Now - LastMoveSentAt).count();
		if (LastMoveSentAt.time_since_epoch().count() != 0 && Elapsed < static_cast<int64>(MoveIntervalMs))
			return;

		LastMoveSentAt = Now;
		SendMove();
	}

	void BotSession::SendLogin()
	{
		Protocol::C_LOGIN Login;
		Login.set_id("loadbot");
		Login.set_pw("loadbot");
		Send(MakeBotSendBuffer(Login, PKT_C_LOGIN));
	}

	void BotSession::SendEnterGame()
	{
		Protocol::C_ENTER_GAME Enter;
		Send(MakeBotSendBuffer(Enter, PKT_C_ENTER_GAME));
	}

	void BotSession::SendMove()
	{
		// 4방향 중 하나 랜덤 선택. 대각선 없음(서버와 동일 규약).
		std::uniform_int_distribution<int32> DirDist(0, 3);
		const int32 Dir = DirDist(RandomEngine);

		const uint64 Seq = NextMoveSeq++;

		Protocol::C_MOVE Move;
		Move.set_dir(static_cast<Protocol::Direction>(Dir));
		Move.set_client_seq(Seq);

		Send(MakeBotSendBuffer(Move, PKT_C_MOVE));
	}

	void BotSession::DispatchPacket(BYTE* Buffer, int32 Len)
	{
		if (Len < static_cast<int32>(sizeof(PacketHeader)))
			return;

		const PacketHeader* Header = reinterpret_cast<const PacketHeader*>(Buffer);
		const BYTE* Payload = Buffer + sizeof(PacketHeader);
		const int32 PayloadSize = Len - static_cast<int32>(sizeof(PacketHeader));

		switch (Header->Id)
		{
		case PKT_S_LOGIN:
		{
			Protocol::S_LOGIN Packet;
			if (Packet.ParseFromArray(Payload, PayloadSize) == false)
				return;

			// 로그인 성공 — 게임 방 입장 요청으로 전이한다.
			State.store(EBotState::Entering, std::memory_order_relaxed);
			SendEnterGame();
			break;
		}
		case PKT_S_ENTER_GAME:
		{
			Protocol::S_ENTER_GAME Packet;
			if (Packet.ParseFromArray(Payload, PayloadSize) == false)
				return;

			// InGame 전이 — 이후 PumpMove 에서 C_MOVE 가 발사된다.
			State.store(EBotState::InGame, std::memory_order_relaxed);
			// 첫 PumpMove 에서 바로 쏘도록 LastMoveSentAt 은 기본값(epoch) 유지.
			break;
		}
		case PKT_S_MOVE:
		case PKT_S_MOVE_REJECT:
		case PKT_S_SPAWN:
		default:
			// 응답/브로드캐스트 패킷은 봇 입장에서 무시한다.
			break;
		}
	}
}