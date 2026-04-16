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

	void BotSession::SweepTimeoutPendingMoves(std::chrono::steady_clock::time_point Now)
	{
		// kPendingTimeoutMs 를 넘긴 in-flight C_MOVE 항목을 제거하고 TimeoutCount 를 누적한다.
		std::lock_guard<std::mutex> Lock(PendingMutex);
		for (auto It = PendingMoves.begin(); It != PendingMoves.end(); )
		{
			const uint64 ElapsedMs = static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(Now - It->second).count());
			if (ElapsedMs >= kPendingTimeoutMs)
			{
				It = PendingMoves.erase(It);
				TimeoutCount.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				++It;
			}
		}
	}

	void BotSession::PumpMove()
	{
		if (State.load(std::memory_order_relaxed) != EBotState::InGame)
			return;

		const std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::now();

		// timeout 스윕 — kPendingTimeoutMs 를 넘긴 in-flight 항목을 제거한다.
		SweepTimeoutPendingMoves(Now);

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
		const std::chrono::steady_clock::time_point SentAt = std::chrono::steady_clock::now();

		Protocol::C_MOVE Move;
		Move.set_dir(static_cast<Protocol::Direction>(Dir));
		Move.set_client_seq(Seq);

		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			// 현재 송신 시간과 seq를 쌍으로 맵에 저장
			PendingMoves.emplace(Seq, SentAt);
		}

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

			// PlayerID 확정 후 InGame 전이 — 이후 PumpMove 에서 C_MOVE 가 발사된다.
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

			// seq echo-back 매칭 — 레거시(client_seq=0) 패킷은 무시한다.
			const uint64 Seq = Packet.client_seq();
			if (Seq == 0)
				return;

			std::chrono::steady_clock::time_point SentAt;
			{
				std::lock_guard<std::mutex> Lock(PendingMutex);
				// seq로 송신 시간을 꺼냅니다.
				auto It = PendingMoves.find(Seq);
				if (It == PendingMoves.end())
					return; 
				SentAt = It->second;
				PendingMoves.erase(It);
			}
				
				// 현재 시간과 송신 시간의 차이로 RTT를 구합니다. 
			const std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::now();
			const double RttMs = std::chrono::duration<double, std::milli>(Now - SentAt).count();

				// 구한 RTT는 샘플 배열에 집어넣습니다.
			{
				std::lock_guard<std::mutex> Lock(SampleMutex);
				Samples.push_back(FLatencySample{ Now, RttMs });
			}
			ReceivedMoveCount.fetch_add(1, std::memory_order_relaxed);
			break;
		}
		case PKT_S_MOVE_REJECT:
		{
			// 서버가 이동을 거절한 경우 — 타일 차단 / 다른 플레이어 점유 / 제자리 이동 중 하나.
			// latency/TPS 샘플에는 포함하지 않고 DropCount 만 누적해 성공률을 정직하게 측정한다.
			Protocol::S_MOVE_REJECT Packet;
			if (Packet.ParseFromArray(Payload, PayloadSize) == false)
				return;

			// 자기 거절만 카운트한다.
			if (Packet.player_id() != MyPlayerID || MyPlayerID == 0)
				return;

			// seq echo-back 매칭 — PendingMoves 에서 제거해 Timeout 스윕과의 이중 계산을 막는다.
			const uint64 Seq = Packet.client_seq();
			if (Seq == 0)
				return;

			{
				std::lock_guard<std::mutex> Lock(PendingMutex);
				auto It = PendingMoves.find(Seq);
				if (It == PendingMoves.end())
					return; // 이미 timeout 스윕으로 제거되었거나 모르는 seq
				PendingMoves.erase(It);
			}

			DropCount.fetch_add(1, std::memory_order_relaxed);
			break;
		}
		case PKT_S_SPAWN:
		default:
			// 다른 패킷들은 봇 입장에서 무시해도 되는 브로드캐스트/예외 회신.
			break;
		}
	}
}