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
		 */
		template<typename T>
		D1::SendBufferRef MakeBotSendBuffer(T& Packet, uint16 PacketId)
		{
			const uint16 DataSize = static_cast<uint16>(Packet.ByteSizeLong());
			const uint16 PacketSize = DataSize + sizeof(D1::PacketHeader);

			D1::SendBufferRef Buffer = D1::SendBufferManager::Get().Open(PacketSize);
			D1::PacketHeader* Header = reinterpret_cast<D1::PacketHeader*>(Buffer->Buffer());
			Header->Size = PacketSize;
			Header->Id = PacketId;
			Packet.SerializeToArray(&Header[1], DataSize);
			Buffer->Close(PacketSize);

			return Buffer;
		}

		/** 스레드·봇별로 충분히 구별되는 시드 값을 만든다. */
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
		D1::Session::OnConnected();

		State.store(EBotState::LoggingIn, std::memory_order_relaxed);
		SendLogin();
	}

	void BotSession::OnDisconnected()
	{
		State.store(EBotState::Closing, std::memory_order_relaxed);
	}

	void BotSession::OnSend(int32 /*NumOfBytes*/)
	{
		// 봇은 송신 완료 로그가 필요 없다 — latency 는 수신 측에서 측정된다.
	}

	void BotSession::OnRecvPacket(BYTE* Buffer, int32 Len)
	{
		DispatchPacket(Buffer, Len);
	}

	void BotSession::RequestShutdown()
	{
		State.store(EBotState::Closing, std::memory_order_relaxed);
	}

	std::vector<FLatencySample> BotSession::DrainSamples()
	{
		std::vector<FLatencySample> Out;
		{
			std::lock_guard<std::mutex> Lock(SampleMutex);
			Out.swap(Samples);
		}
		return Out;
	}

	void BotSession::PumpMove()
	{
		if (State.load(std::memory_order_relaxed) != EBotState::InGame)
			return;

		const auto Now = std::chrono::steady_clock::now();

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

		Protocol::C_MOVE Move;
		Move.set_dir(static_cast<Protocol::Direction>(Dir));

		const uint64 Seq = NextMoveSeq++;
		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			PendingMoves[Seq] = FPendingMove{ Seq, std::chrono::steady_clock::now() };
		}

		Send(MakeBotSendBuffer(Move, PKT_C_MOVE));
	}

	void BotSession::DispatchPacket(BYTE* Buffer, int32 Len)
	{
		if (Len < static_cast<int32>(sizeof(D1::PacketHeader)))
			return;

		const D1::PacketHeader* Header = reinterpret_cast<const D1::PacketHeader*>(Buffer);
		const BYTE* Payload = Buffer + sizeof(D1::PacketHeader);
		const int32 PayloadSize = Len - sizeof(D1::PacketHeader);

		switch (Header->Id)
		{
		case PKT_S_LOGIN:
		{
			Protocol::S_LOGIN Packet;
			if (Packet.ParseFromArray(Payload, PayloadSize) == false)
				return;

			State.store(EBotState::Entering, std::memory_order_relaxed);
			SendEnterGame();
			break;
		}
		case PKT_S_ENTER_GAME:
		{
			Protocol::S_ENTER_GAME Packet;
			if (Packet.ParseFromArray(Payload, PayloadSize) == false)
				return;

			MyPlayerID = Packet.player_id();
			State.store(EBotState::InGame, std::memory_order_relaxed);
			// 첫 PumpMove 에서 바로 쏘도록 LastMoveSentAt 은 기본값(epoch) 유지.
			break;
		}
		case PKT_S_MOVE:
		{
			Protocol::S_MOVE Packet;
			if (Packet.ParseFromArray(Payload, PayloadSize) == false)
				return;

			// 서버 S_MOVE 는 방 전체 브로드캐스트다. 내 ID 와 일치하는 것만 latency 샘플에 반영한다.
			if (Packet.player_id() != MyPlayerID || MyPlayerID == 0)
				return;

			// FIFO 대응: 가장 오래된 pending 을 소비한다. (서버 응답이 요청 순서대로 온다는 가정)
			std::chrono::steady_clock::time_point SentAt;
			bool bFound = false;
			{
				std::lock_guard<std::mutex> Lock(PendingMutex);
				if (PendingMoves.empty() == false)
				{
					uint64 OldestSeq = UINT64_MAX;
					for (const auto& Pair : PendingMoves)
					{
						if (Pair.first < OldestSeq)
							OldestSeq = Pair.first;
					}
					SentAt = PendingMoves[OldestSeq].SentAt;
					PendingMoves.erase(OldestSeq);
					bFound = true;
				}
			}

			if (bFound == false)
				return;

			const auto Now = std::chrono::steady_clock::now();
			const double RttMs = std::chrono::duration<double, std::milli>(Now - SentAt).count();

			{
				std::lock_guard<std::mutex> Lock(SampleMutex);
				Samples.push_back(FLatencySample{ Now, RttMs });
			}
			ReceivedMoveCount.fetch_add(1, std::memory_order_relaxed);
			break;
		}
		case PKT_S_SPAWN:
		case PKT_S_MOVE_REJECT:
		default:
			// 다른 패킷들은 봇 입장에서 무시해도 되는 브로드캐스트/예외 회신.
			break;
		}
	}
}