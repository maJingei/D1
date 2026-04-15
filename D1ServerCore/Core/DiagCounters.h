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
// 배열 크기는 GameRoomManager::ROOM_COUNT 와 동일하게 유지할 것.
static constexpr int DIAG_ROOM_COUNT = 4;
extern std::atomic<uint64_t> GRoomTryMoveCount[DIAG_ROOM_COUNT];