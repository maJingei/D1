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

// LOG LOGIC : recv/send 카운터 정의 (DiagCounters.h 의 extern 선언과 1:1 매핑)
std::atomic<uint64_t> GRecvPacketCount{ 0 };
std::atomic<uint64_t> GSendPacketCount{ 0 };

// LOG LOGIC : DoBroadcast latency 분포 ring buffer 정의
std::atomic<uint64_t> GBroadcastNsRing[BROADCAST_NS_RING_SIZE] = {};
std::atomic<uint64_t> GBroadcastNsRingIdx{ 0 };