#pragma once

#include <atomic>
#include <chrono>
#include <random>
#include <string>

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

	/**
	 * 부하 봇 전용 PacketSession.
	 *
	 * 시나리오: OnConnected → C_LOGIN → S_LOGIN → C_ENTER_GAME → S_ENTER_GAME → 주기 C_MOVE.
	 * 응답 패킷(S_MOVE / S_MOVE_REJECT)은 수신 후 별도 처리 없이 drop 한다.
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
		 * InGame 상태일 때 MoveIntervalMs 주기에 맞춰 C_MOVE 를 송신한다.
		 */
		void PumpMove();

		/** 봇에게 이동 스레드 종료 & 연결 끊기 지시. State 를 Closing 으로 전환한다. */
		void RequestShutdown();

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

		/** 랜덤 Direction 으로 C_MOVE 를 송신한다. */
		void SendMove();

		/** 수신 버퍼를 파싱해 S_LOGIN / S_ENTER_GAME 만 상태 전이에 사용한다. */
		void DispatchPacket(BYTE* Buffer, int32 Len);

	private:
		/** 현재 연결·핸드쉐이크 상태. */
		std::atomic<EBotState> State;

		/** C_MOVE 전송 주기 (밀리초). SetMoveInterval 로 설정된다. */
		uint32 MoveIntervalMs = 200;

		/** C_MOVE 로컬 시퀀스. 이동 순서 어긋남·중복 응답 대비로 1 부터 단조 증가. */
		uint64 NextMoveSeq = 1;

		/** 최근 C_MOVE 전송 시각. 이 값 + MoveIntervalMs <= now 일 때 다음 MOVE 발사. */
		std::chrono::steady_clock::time_point LastMoveSentAt{};

		/** 4방향 이동 방향 랜덤 발생기 — 봇마다 독립 시드로 초기화된다. */
		std::mt19937 RandomEngine;
	};
}