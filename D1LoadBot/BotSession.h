#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "Iocp/PacketSession.h"

namespace D1LoadBot
{
	/** 봇 세션의 연결 상태 — 핸들러 디스패치용 단순 상태 머신. */
	enum class EBotState : uint8
	{
		Connecting,
		LoggingIn,
		Entering,
		InGame,
		Closing,
	};

	/** C_MOVE 전송 시각을 기록하는 엔트리. RTT 계산에 사용된다. */
	struct FPendingMove
	{
		uint64 Seq = 0;
		std::chrono::steady_clock::time_point SentAt;
	};

	/** BotSession 이 보고하는 측정 샘플. 글로벌 수집기(Metrics)가 소유한다. */
	struct FLatencySample
	{
		std::chrono::steady_clock::time_point At;
		double RttMs = 0.0;
	};

	/**
	 * 부하 봇 전용 PacketSession.
	 *
	 * 시나리오: OnConnected → C_LOGIN → S_LOGIN → C_ENTER_GAME → S_ENTER_GAME → 주기 C_MOVE.
	 * C_MOVE 전송 시 PendingMoves 에 현재 tick 을 저장하고, S_MOVE 응답(자기 것)에서 RTT 를 계산한다.
	 */
	class BotSession : public D1::PacketSession
	{
	public:
		BotSession();
		~BotSession() override = default;

		/** 이동 주기 설정 (Bot 생성 직후 1회). */
		void SetMoveInterval(uint32 IntervalMs) { MoveIntervalMs = IntervalMs; }

		/** 외부 펌프(1 tick)에서 호출: InGame 상태일 때 주기에 맞춰 C_MOVE 를 송신한다. */
		void PumpMove();

		/** 봇에게 이동 스레드 종료 & 연결 끊기 지시. */
		void RequestShutdown();

		/** 지금까지 수집한 latency 샘플(자기 자신의 C_MOVE→S_MOVE RTT)을 꺼낸다. */
		std::vector<FLatencySample> DrainSamples();

		/** 이 봇이 자기 S_MOVE 를 누적 수신한 횟수 (debug). */
		uint64 GetReceivedMoveCount() const { return ReceivedMoveCount.load(std::memory_order_relaxed); }

	protected:
		void OnConnected() override;
		void OnRecvPacket(BYTE* Buffer, int32 Len) override;
		void OnSend(int32 NumOfBytes) override;
		void OnDisconnected() override;

	private:
		/** C_LOGIN 페이로드를 만들어 서버로 보낸다. */
		void SendLogin();

		/** C_ENTER_GAME 페이로드(빈 payload)를 서버로 보낸다. */
		void SendEnterGame();

		/** 랜덤 Direction 으로 C_MOVE 를 보낸다 + PendingMoves 에 타임스탬프를 남긴다. */
		void SendMove();

		/** 패킷 버퍼를 보고 S_LOGIN/S_ENTER_GAME/S_MOVE 중 하나를 처리한다. */
		void DispatchPacket(BYTE* Buffer, int32 Len);

	private:
		std::atomic<EBotState> State;

		// 이동 주기.
		uint32 MoveIntervalMs = 200;

		// 내 PlayerID (S_ENTER_GAME 에서 확정). 자기 S_MOVE 인지 필터링에 사용.
		uint64 MyPlayerID = 0;

		// C_MOVE 로컬 시퀀스. 이동 순서 어긋남/중복 응답에 대비해 1 부터 증가.
		uint64 NextMoveSeq = 1;

		// 최근 전송 시각. 이 값 + MoveIntervalMs <= now 일 때 다음 MOVE 발사.
		std::chrono::steady_clock::time_point LastMoveSentAt{};

		// 요청 중인 C_MOVE 기록. 한 봇이 한 번에 하나의 이동만 잠기므로 vector 수준이면 충분하지만
		// 누락/재요청 보호를 위해 seq → sentAt map 을 유지한다.
		std::mutex PendingMutex;
		std::unordered_map<uint64, FPendingMove> PendingMoves;

		// 측정 결과 누적.
		std::mutex SampleMutex;
		std::vector<FLatencySample> Samples;

		// 디버그/스모크용 카운터.
		std::atomic<uint64> ReceivedMoveCount{ 0 };

		// 이동 방향 랜덤 발생기 — 봇마다 독립 시드.
		std::mt19937 RandomEngine;
	};
}