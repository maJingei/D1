// LOG LOGIC FILE : 서버 측 처리량/부하 측정 CSV 모듈 구현.
#include "MetricsCsvWriter.h"
#include "Core/DiagCounters.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <vector>

namespace MetricsCsvWriter
{
	// LOG LOGIC : 모듈 전역 상태
	static std::atomic<bool> bEnabled{ false };
	static std::string OutputPath;
	static uint32_t RoomCount = 0;

	// LOG LOGIC : bucket 0 시각 (steady_clock epoch ns). 0 = 아직 시작 안 됨.
	static std::atomic<int64_t> StartTimeNs{ 0 };

	// LOG LOGIC : 직전 Tick 시점의 누계 카운터 (delta 계산용)
	// reporter thread 1개에서만 호출되므로 평범한 변수로 충분.
	static uint64_t PrevRecv = 0;
	static uint64_t PrevSend = 0;
	static uint64_t PrevCalls = 0;
	static uint64_t PrevTargets = 0;
	static uint64_t PrevNsSum = 0;
	static uint64_t PrevRingIdx = 0;

	// LOG LOGIC : 1초 단위 누적 bucket
	struct FBucket
	{
		uint32_t SecondIndex;
		uint64_t RecvPps;
		uint64_t SendPps;
		uint64_t BroadcastCalls;
		uint64_t BroadcastTargetSum;
		uint64_t BroadcastAvgNs;
		uint64_t BroadcastP95Ns;
	};
	static std::vector<FBucket> Buckets;
	static std::mutex BucketsMutex; // Tick(reporter) ↔ Flush(메인) 경쟁 차단

	// LOG LOGIC : ring buffer 의 직전 1초 sample 에서 P95 추출.
	// PushCountThisSecond 가 ring 크기를 초과하면 ring 크기로 클램프 — 가장 최근 N개로 근사.
	static uint64_t ComputeBroadcastP95Ns(uint64_t PushCountThisSecond)
	{
		const uint64_t SampleCount = std::min<uint64_t>(PushCountThisSecond, BROADCAST_NS_RING_SIZE);
		if (SampleCount == 0) return 0;

		// 현재 인덱스 직전부터 SampleCount 개를 역순으로 dump.
		const uint64_t CurIdx = GBroadcastNsRingIdx.load(std::memory_order_relaxed);
		std::vector<uint64_t> Samples;
		Samples.reserve(static_cast<size_t>(SampleCount));
		for (uint64_t i = 0; i < SampleCount; ++i)
		{
			const uint64_t SlotIdx = (CurIdx - 1 - i) & (BROADCAST_NS_RING_SIZE - 1);
			Samples.push_back(GBroadcastNsRing[SlotIdx].load(std::memory_order_relaxed));
		}

		// nth_element 로 P95 인덱스 위치만 부분 정렬 — O(N) 평균.
		const size_t P95RawIdx = static_cast<size_t>((SampleCount * 95) / 100);
		const size_t P95Idx = (P95RawIdx >= Samples.size()) ? (Samples.size() - 1) : P95RawIdx;
		std::nth_element(Samples.begin(), Samples.begin() + P95Idx, Samples.end());
		return Samples[P95Idx];
	}

	void Initialize(bool bInEnabled, std::string InOutputPath, uint32_t InRoomCount)
	{
		bEnabled.store(bInEnabled, std::memory_order_relaxed);
		OutputPath = std::move(InOutputPath);
		RoomCount = InRoomCount;
	}

	void TryStartTimeline()
	{
		if (!bEnabled.load(std::memory_order_relaxed)) return;

		int64_t Expected = 0;
		const int64_t Now = std::chrono::steady_clock::now().time_since_epoch().count();
		StartTimeNs.compare_exchange_strong(Expected, Now, std::memory_order_acq_rel);
	}

	void Tick()
	{
		if (!bEnabled.load(std::memory_order_relaxed)) return;

		const int64_t StartNs = StartTimeNs.load(std::memory_order_relaxed);
		if (StartNs == 0) return; // 첫 세션 진입 전 → 아직 측정 시작 안 함

		// SecondIndex = 시작 시점 기준 경과 초.
		const auto Now = std::chrono::steady_clock::now();
		const int64_t NowNs = Now.time_since_epoch().count();
		const uint32_t SecondIdx = static_cast<uint32_t>((NowNs - StartNs) / 1'000'000'000LL);

		// DiagCounters 누계값 snapshot
		const uint64_t Recv = GRecvPacketCount.load(std::memory_order_relaxed);
		const uint64_t Send = GSendPacketCount.load(std::memory_order_relaxed);
		const uint64_t Calls = GDoBroadcastCallCount.load(std::memory_order_relaxed);
		const uint64_t Targets = GDoBroadcastSessionSum.load(std::memory_order_relaxed);
		const uint64_t NsSum = GDoBroadcastNsSum.load(std::memory_order_relaxed);
		const uint64_t RingIdx = GBroadcastNsRingIdx.load(std::memory_order_relaxed);

		// delta 계산
		const uint64_t DeltaRecv = Recv - PrevRecv;
		const uint64_t DeltaSend = Send - PrevSend;
		const uint64_t DeltaCalls = Calls - PrevCalls;
		const uint64_t DeltaTargets = Targets - PrevTargets;
		const uint64_t DeltaNsSum = NsSum - PrevNsSum;
		const uint64_t DeltaPushed = RingIdx - PrevRingIdx;

		PrevRecv = Recv;
		PrevSend = Send;
		PrevCalls = Calls;
		PrevTargets = Targets;
		PrevNsSum = NsSum;
		PrevRingIdx = RingIdx;

		const uint64_t AvgNs = (DeltaCalls > 0) ? (DeltaNsSum / DeltaCalls) : 0;
		const uint64_t P95Ns = ComputeBroadcastP95Ns(DeltaPushed);

		FBucket B;
		B.SecondIndex = SecondIdx;
		B.RecvPps = DeltaRecv;
		B.SendPps = DeltaSend;
		B.BroadcastCalls = DeltaCalls;
		B.BroadcastTargetSum = DeltaTargets;
		B.BroadcastAvgNs = AvgNs;
		B.BroadcastP95Ns = P95Ns;

		{
			std::lock_guard<std::mutex> Lock(BucketsMutex);
			Buckets.push_back(B);
		}

		// LOG LOGIC : 매 초 csv overwrite 하여 종료 시퀀스와 무관하게 즉시 확인 가능.
		// Flush 가 자체 lock 을 잡으므로 위 scope 와 분리되어 deadlock 없음.
		Flush();
	}

	void Flush()
	{
		if (!bEnabled.load(std::memory_order_relaxed)) return;

		std::lock_guard<std::mutex> Lock(BucketsMutex);

		std::ofstream File(OutputPath);
		if (!File.is_open())
		{
			std::cout << "[MetricsCsvWriter] Failed to open: " << OutputPath << std::endl;
			return;
		}

		// meta 합계 산출
		uint64_t TotalRecv = 0;
		uint64_t TotalSend = 0;
		uint64_t TotalCalls = 0;
		uint64_t TotalTargets = 0;
		for (const FBucket& B : Buckets)
		{
			TotalRecv += B.RecvPps;
			TotalSend += B.SendPps;
			TotalCalls += B.BroadcastCalls;
			TotalTargets += B.BroadcastTargetSum;
		}
		const double Amplification = (TotalCalls > 0) ? (static_cast<double>(TotalTargets) / static_cast<double>(TotalCalls)) : 0.0;

		// LoadBot Metrics.h 와 동일한 section,key,value + 빈 줄 + per-second 표 패턴.
		File << "section,key,value\n";
		File << "meta,room_count," << RoomCount << "\n";
		File << "meta,duration_sec," << Buckets.size() << "\n";
		File << "meta,total_recv_packets," << TotalRecv << "\n";
		File << "meta,total_send_packets," << TotalSend << "\n";
		File << "meta,total_broadcast_calls," << TotalCalls << "\n";
		File << "meta,total_broadcast_targets," << TotalTargets << "\n";
		File << "meta,broadcast_amplification_ratio_avg," << std::fixed << std::setprecision(2) << Amplification << "\n";

		File << "\nsecond,recv_pps_total,send_pps_total,broadcast_calls,broadcast_target_sum,broadcast_avg_ns,broadcast_p95_ns\n";
		for (const FBucket& B : Buckets)
		{
			File << B.SecondIndex << ","
				<< B.RecvPps << ","
				<< B.SendPps << ","
				<< B.BroadcastCalls << ","
				<< B.BroadcastTargetSum << ","
				<< B.BroadcastAvgNs << ","
				<< B.BroadcastP95Ns << "\n";
		}

		File.close();
		std::cout << "[MetricsCsvWriter] Flushed " << Buckets.size() << " seconds to " << OutputPath << std::endl;
	}
}
