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
	/**
	 * 봇 세션의 연결·핸드쉐이크 상태.
	 *
	 * OnConnected → LoggingIn → Entering → InGame 순으로 전이하며,
	 * 연결이 끊기거나 RequestShutdown 이 호출되면 Closing 으로 전환된다.
	 */
	enum class EBotState : uint8
	{
		Connecting, // TCP 연결 완료 대기
		LoggingIn,  // C_LOGIN 송신 후 S_LOGIN 대기
		Entering,   // C_ENTER_GAME 송신 후 S_ENTER_GAME 대기
		InGame,     // 정상 플레이 중 — C_MOVE 주기 송신 활성
		Closing,    // 종료 진행 중 — PumpMove 가 아무것도 하지 않는다
	};

	/** BotSession 이 보고하는 RTT 측정 샘플. 글로벌 수집기(Metrics)가 소유한다. */
	struct FLatencySample
	{
		/** 수신 시각 — 버킷 인덱스 산정에 사용된다. */
		std::chrono::steady_clock::time_point At;

		/** C_MOVE 송신 ~ S_MOVE 수신 왕복 지연 (밀리초). */
		double RttMs = 0.0;
	};

	/**
	 * 부하 봇 전용 PacketSession.
	 *
	 * 시나리오: OnConnected → C_LOGIN → S_LOGIN → C_ENTER_GAME → S_ENTER_GAME → 주기 C_MOVE.
	 * C_MOVE 전송 시 PendingMoves 에 현재 tick 을 저장하고,
	 * S_MOVE 응답(자기 것)에서 seq echo-back 방식으로 RTT 를 계산한다.
	 */
	class BotSession : public PacketSession
	{
	public:
		BotSession();
		~BotSession() override = default;

		/** 이동 주기 설정 (Bot 생성 직후 1회 호출). */
		void SetMoveInterval(uint32 IntervalMs) { MoveIntervalMs = IntervalMs; }

		/**
		 * 외부 펌프 스레드(1 tick)에서 호출한다.
		 *
		 * InGame 상태일 때 MoveIntervalMs 주기에 맞춰 C_MOVE 를 송신하고,
		 * kPendingTimeoutMs 를 초과한 in-flight 항목을 스윕해 TimeoutCount 를 누적한다.
		 */
		void PumpMove();

		/** 봇에게 이동 스레드 종료 & 연결 끊기 지시. State 를 Closing 으로 전환한다. */
		void RequestShutdown();

		/**
		 * 지금까지 수집한 latency 샘플(C_MOVE→S_MOVE RTT)을 꺼내 반환하고 내부 버퍼를 비운다.
		 *
		 * @return  이 호출 시점까지 쌓인 FLatencySample 목록
		 */
		std::vector<FLatencySample> DrainSamples();

		/** 이 봇이 자기 S_MOVE 를 누적 수신한 횟수 (디버그/스모크 확인용). */
		uint64 GetReceivedMoveCount() const { return ReceivedMoveCount.load(std::memory_order_relaxed); }

		/** 누적된 drop 카운터를 get-and-reset 한다. Metrics drain 주기마다 호출된다. */
		uint64 DrainDropCount() { return DropCount.exchange(0, std::memory_order_relaxed); }

		/** 누적된 timeout 카운터를 get-and-reset 한다. Metrics drain 주기마다 호출된다. */
		uint64 DrainTimeoutCount() { return TimeoutCount.exchange(0, std::memory_order_relaxed); }

	protected:
		void OnConnected() override;
		void OnRecvPacket(BYTE* Buffer, int32 Len) override;
		void OnSend(int32 NumOfBytes) override;
		void OnDisconnected() override;

	private:
		/** C_LOGIN 페이로드를 직렬화해 서버로 송신한다. */
		void SendLogin();

		/** C_ENTER_GAME 페이로드(빈 payload)를 서버로 송신한다. */
		void SendEnterGame();

		/** 랜덤 Direction 으로 C_MOVE 를 송신하고 PendingMoves 에 타임스탬프를 기록한다. */
		void SendMove();

		/** 수신 버퍼를 파싱해 S_LOGIN / S_ENTER_GAME / S_MOVE / S_MOVE_REJECT 중 하나를 처리한다. */
		void DispatchPacket(BYTE* Buffer, int32 Len);

		/**
		 * kPendingTimeoutMs 를 초과한 in-flight C_MOVE 항목을 PendingMoves 에서 제거한다.
		 *
		 * PendingMutex 를 내부에서 취득한다. PumpMove 가 매 tick 호출한다.
		 *
		 * @param Now  현재 시각 (PumpMove 에서 측정한 값을 재사용해 중복 측정 방지)
		 */
		void SweepTimeoutPendingMoves(std::chrono::steady_clock::time_point Now);

	private:
		/** 현재 연결·핸드쉐이크 상태. */
		std::atomic<EBotState> State;

		/** C_MOVE 전송 주기 (밀리초). SetMoveInterval 로 설정된다. */
		uint32 MoveIntervalMs = 200;

		/** 내 PlayerID — S_ENTER_GAME 에서 확정된다. 자기 S_MOVE 인지 필터링에 사용. */
		uint64 MyPlayerID = 0;

		/** C_MOVE 로컬 시퀀스. 이동 순서 어긋남·중복 응답 대비로 1 부터 단조 증가. */
		uint64 NextMoveSeq = 1;

		/** 최근 C_MOVE 전송 시각. 이 값 + MoveIntervalMs <= now 일 때 다음 MOVE 발사. */
		std::chrono::steady_clock::time_point LastMoveSentAt{};

		/** in-flight C_MOVE 기록. key=client_seq, value=SentAt. seq echo-back 으로 RTT 를 계산한다. */
		std::mutex PendingMutex;
		std::unordered_map<uint64, std::chrono::steady_clock::time_point> PendingMoves;

		/** DrainSamples 로 소비되는 RTT 샘플 버퍼. */
		std::mutex SampleMutex;
		std::vector<FLatencySample> Samples;

		/** 자기 S_MOVE 를 수신한 총 횟수 (디버그/스모크용). */
		std::atomic<uint64> ReceivedMoveCount{ 0 };

		/**
		 * 서버가 S_MOVE_REJECT 로 거절한 이동 횟수.
		 *
		 * 거절 사유(D1Server/GameRoom.cpp 참조): 타일 차단 / 다른 플레이어 점유 / 제자리 이동.
		 * 부하 테스트에서 세션이 한 방에 몰리면 점유 충돌로 빈번히 발생하므로 정직한 성공률 측정을 위해 추적한다.
		 */
		std::atomic<uint64> DropCount{ 0 };

		/** kPendingTimeoutMs 를 초과해 스윕된 in-flight 항목 수. */
		std::atomic<uint64> TimeoutCount{ 0 };

		/** pending timeout 임계값 (ms). 서버 정상 응답 시간의 100배 이상을 안전망으로 설정. */
		static constexpr uint64 kPendingTimeoutMs = 5000;

		/** 4방향 이동 방향 랜덤 발생기 — 봇마다 독립 시드로 초기화된다. */
		std::mt19937 RandomEngine;
	};
}