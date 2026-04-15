#pragma once

// Windows.h 가 어딘가에서 min/max 매크로를 정의하는 것을 차단한다 — std::min/std::max 토큰화 깨짐 방지.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "BotSession.h"
#include "Core/Types.h"

namespace D1LoadBot
{
	/** 1초 단위로 누적된 측정 버킷. CSV 한 행에 대응한다. */
	struct FMetricsBucket
	{
		uint32 SecondIndex = 0;
		uint64 MoveRecvCount = 0; // 이 초 구간에 수신한 자기 S_MOVE 개수 — TPS 환산
		std::vector<double> LatenciesMs; // 이 초 구간의 RTT 샘플
	};

	/**
	 * 전체 실행 동안의 측정 결과를 집계하고 CSV 로 저장한다.
	 *
	 * 설계: BotSession 마다 std::vector<FLatencySample> 을 들고 있고,
	 * 메인 루프가 매초 수집 스레드에서 DrainSamples() 로 긁어와 여기에 Merge 한다.
	 */
	class Metrics
	{
	public:
		/** 시작 시각을 고정한다. 이후 버킷 인덱스는 이 시점 기준 초 단위. */
		void Start()
		{
			StartTime = std::chrono::steady_clock::now();
		}

		/**
		 * 샘플 묶음을 병합한다.
		 *
		 * @param Samples  해당 주기 동안 누적된 latency 샘플
		 */
		void Merge(const std::vector<FLatencySample>& Samples)
		{
			for (const FLatencySample& Sample : Samples)
			{
				const uint32 Idx = BucketIndexFor(Sample.At);
				FMetricsBucket& Bucket = EnsureBucket(Idx);
				Bucket.MoveRecvCount++;
				Bucket.LatenciesMs.push_back(Sample.RttMs);
			}
		}

		/** stdout 요약 + CSV 기록. */
		void Report(const std::string& CsvPath, uint32 DurationSec, uint32 SessionCount, uint32 MoveIntervalMs)
		{
			std::vector<double> AllLatencies;
			AllLatencies.reserve(1024);
			uint64 TotalMoves = 0;
			uint64 PeakPerSecond = 0;
			for (const FMetricsBucket& Bucket : Buckets)
			{
				TotalMoves += Bucket.MoveRecvCount;
				if (Bucket.MoveRecvCount > PeakPerSecond)
					PeakPerSecond = Bucket.MoveRecvCount;
				for (double L : Bucket.LatenciesMs)
					AllLatencies.push_back(L);
			}

			const double P50 = Percentile(AllLatencies, kP50);
			const double P95 = Percentile(AllLatencies, kP95);
			const double P99 = Percentile(AllLatencies, kP99);
			const double AvgTps = (DurationSec > 0) ? (static_cast<double>(TotalMoves) / DurationSec) : 0.0;

			std::printf("[D1LoadBot] ==== Benchmark Summary ====\n");
			std::printf("[D1LoadBot] Sessions        : %u\n", SessionCount);
			std::printf("[D1LoadBot] MoveIntervalMs  : %u\n", MoveIntervalMs);
			std::printf("[D1LoadBot] DurationSec     : %u\n", DurationSec);
			std::printf("[D1LoadBot] TotalMoveRecv   : %llu\n", static_cast<unsigned long long>(TotalMoves));
			std::printf("[D1LoadBot] AvgTps          : %.2f\n", AvgTps);
			std::printf("[D1LoadBot] PeakTps         : %llu\n", static_cast<unsigned long long>(PeakPerSecond));
			std::printf("[D1LoadBot] LatencyP50 (ms) : %.3f\n", P50);
			std::printf("[D1LoadBot] LatencyP95 (ms) : %.3f\n", P95);
			std::printf("[D1LoadBot] LatencyP99 (ms) : %.3f\n", P99);
			std::printf("[D1LoadBot] Samples         : %zu\n", AllLatencies.size());

			WriteCsv(CsvPath, P50, P95, P99, AvgTps, PeakPerSecond, TotalMoves, DurationSec, SessionCount, MoveIntervalMs);
		}

	private:
		uint32 BucketIndexFor(std::chrono::steady_clock::time_point At) const
		{
			const auto Delta = std::chrono::duration_cast<std::chrono::seconds>(At - StartTime).count();
			return (Delta < 0) ? 0u : static_cast<uint32>(Delta);
		}

		FMetricsBucket& EnsureBucket(uint32 Idx)
		{
			while (Buckets.size() <= Idx)
			{
				FMetricsBucket Bucket;
				Bucket.SecondIndex = static_cast<uint32>(Buckets.size());
				Buckets.push_back(std::move(Bucket));
			}
			return Buckets[Idx];
		}

		static double Percentile(std::vector<double>& Values, double Ratio)
		{
			if (Values.empty())
				return 0.0;
			std::sort(Values.begin(), Values.end());
			const size_t MaxIdx = Values.size() - 1;
			const double Raw = Ratio * static_cast<double>(MaxIdx);
			const size_t Idx = static_cast<size_t>(Raw);
			return Values[(Idx < MaxIdx) ? Idx : MaxIdx];
		}

		void WriteCsv(const std::string& Path,
			double P50, double P95, double P99,
			double AvgTps, uint64 PeakTps,
			uint64 TotalMoves, uint32 DurationSec,
			uint32 SessionCount, uint32 MoveIntervalMs)
		{
			std::ofstream File(Path);
			if (File.is_open() == false)
			{
				std::printf("[D1LoadBot] CSV open failed: %s\n", Path.c_str());
				return;
			}

			// 메타 헤더 주석 (첫 줄 '#' 으로 시작 — 흔한 CSV 소비자와 호환을 위해 주석 대신 key-value 섹션으로 기록)
			File << "section,key,value\n";
			File << "meta,sessions," << SessionCount << "\n";
			File << "meta,move_interval_ms," << MoveIntervalMs << "\n";
			File << "meta,duration_sec," << DurationSec << "\n";
			File << "meta,total_move_recv," << TotalMoves << "\n";
			File << "meta,avg_tps," << AvgTps << "\n";
			File << "meta,peak_tps," << PeakTps << "\n";
			File << "meta,latency_p50_ms," << P50 << "\n";
			File << "meta,latency_p95_ms," << P95 << "\n";
			File << "meta,latency_p99_ms," << P99 << "\n";

			// 초당 TPS/latency 샘플 — 시각화용
			File << "\nsecond,move_recv,latency_p50_ms,latency_p95_ms,latency_p99_ms\n";
			for (FMetricsBucket& Bucket : Buckets)
			{
				std::vector<double> Copy = Bucket.LatenciesMs;
				const double BucketP50 = Percentile(Copy, kP50);
				Copy = Bucket.LatenciesMs;
				const double BucketP95 = Percentile(Copy, kP95);
				Copy = Bucket.LatenciesMs;
				const double BucketP99 = Percentile(Copy, kP99);
				File << Bucket.SecondIndex << "," << Bucket.MoveRecvCount << ","
					<< BucketP50 << "," << BucketP95 << "," << BucketP99 << "\n";
			}

			File.close();
			std::printf("[D1LoadBot] CSV written: %s\n", Path.c_str());
		}

		static constexpr double kP50 = 0.50;
		static constexpr double kP95 = 0.95;
		static constexpr double kP99 = 0.99;

		std::chrono::steady_clock::time_point StartTime{};
		std::vector<FMetricsBucket> Buckets;
	};
}