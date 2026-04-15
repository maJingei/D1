#include "DiagCounters.h"

// [DIAG] probe: 전역 atomic 카운터 정의

std::atomic<uint64_t> GWorkerJobCount[FLUSH_WORKER_COUNT] = {};

std::atomic<uint64_t> GDoBroadcastNsSum{ 0 };
std::atomic<uint64_t> GDoBroadcastCallCount{ 0 };
std::atomic<uint64_t> GDoBroadcastSessionSum{ 0 };

std::atomic<uint64_t> GSendAppendCount{ 0 };
std::atomic<uint64_t> GSendRegisterCount{ 0 };

std::atomic<uint64_t> GSendBatchSizeSum{ 0 };
std::atomic<uint64_t> GSendBatchSizeCount{ 0 };

std::atomic<int32_t> GInflightWorker{ 0 };
std::atomic<int32_t> GInflightWorkerMax{ 0 };

std::atomic<uint64_t> GRoomTryMoveCount[DIAG_ROOM_COUNT] = {};