#pragma once

#include <atomic>
#include <cstdint>

/**
 * JobQueue/Send 병목 진단용 전역 atomic 카운터.
 * 측정 후 별도 세션에서 제거 또는 정리 예정.
 */

/** FlushWorker 개수 — D1Server.cpp FlushWorker 루프와 공유. */
static constexpr int FLUSH_WORKER_COUNT = 3;

/** 진단 리포터 출력 주기 (ms) */
static constexpr int DIAG_REPORT_INTERVAL_MS = 1000;

// [DIAG] probe: FlushWorker별 Job 처리 카운터
extern std::atomic<uint64_t> GWorkerJobCount[FLUSH_WORKER_COUNT];

// [DIAG] probe: DoBroadcast 지속시간/호출수/세션수 누계
extern std::atomic<uint64_t> GDoBroadcastNsSum;
extern std::atomic<uint64_t> GDoBroadcastCallCount;
extern std::atomic<uint64_t> GDoBroadcastSessionSum;

// [DIAG] probe: Session::Send 경로 카운터
extern std::atomic<uint64_t> GSendAppendCount;
extern std::atomic<uint64_t> GSendRegisterCount;

// [DIAG] probe: RegisterSend 배치 크기 누계
extern std::atomic<uint64_t> GSendBatchSizeSum;
extern std::atomic<uint64_t> GSendBatchSizeCount;

// [DIAG] probe: FlushJob 동시 실행 중인 Worker 수 게이지
// GInflightWorker: 현재 FlushJob 실행 중인 Worker 수 (진입 +1, 이탈 -1)
// GInflightWorkerMax: 1초 구간 내 최대 동시 실행 수 (리포터가 출력 후 0 으로 리셋)
extern std::atomic<int32_t> GInflightWorker;
extern std::atomic<int32_t> GInflightWorkerMax;

// [DIAG] probe: 방별 TryMove 호출 카운터 (A안 4-방 분할 이후 추가)
// 배열 크기는 World::LEVEL_COUNT 와 동일하게 유지할 것.
static constexpr int DIAG_ROOM_COUNT = 2;
extern std::atomic<uint64_t> GRoomTryMoveCount[DIAG_ROOM_COUNT];

// LOG LOGIC : 서버 측 패킷 수신/송신 횟수 누계 (server_metrics.csv 의 recv_pps/send_pps 산출용)
extern std::atomic<uint64_t> GRecvPacketCount;
extern std::atomic<uint64_t> GSendPacketCount;

// LOG LOGIC : DoBroadcast 1회당 ns 샘플을 적재할 lock-free ring buffer
// 크기는 2의 거듭제곱이라야 비트 마스크로 인덱스를 얻을 수 있다.
// 8192 슬롯이면 broadcast 빈도 ≈5k/sec 시 한 초간의 sample 을 모두 보관한다.
static constexpr uint64_t BROADCAST_NS_RING_SIZE = 8192;
extern std::atomic<uint64_t> GBroadcastNsRing[BROADCAST_NS_RING_SIZE];
extern std::atomic<uint64_t> GBroadcastNsRingIdx;

// LOG LOGIC : DoBroadcast 직후 호출되어 ns 측정값을 ring buffer 에 적재한다.
// fetch_add 로 슬롯을 선점하고 store 로 값을 기록 → lock 없이 다중 스레드 동시 push 가능.
inline void DiagPushBroadcastNs(uint64_t Ns)
{
	const uint64_t Idx = GBroadcastNsRingIdx.fetch_add(1, std::memory_order_relaxed);
	GBroadcastNsRing[Idx & (BROADCAST_NS_RING_SIZE - 1)].store(Ns, std::memory_order_relaxed);
}