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
	/** 1초 단위로 누적된 측정 버킷. CSV 초당 행 한 줄에 대응한다. */
	struct FMetricsBucket
	{
		/** StartTime 기준 몇 번째 초인지. */
		uint32 SecondIndex = 0;

		/** 이 초 구간에 수신한 자기 S_MOVE 개수 — TPS 환산 기준. */
		uint64 MoveRecvCount = 0;

		/** 이 초 구간의 RTT 샘플 목록 (밀리초). Percentile 계산에 사용된다. */
		std::vector<double> LatenciesMs;
	};

	/** Report 에서 계산된 전체 구간 요약 통계. WriteCsv 와 printf 에 공통으로 사용된다. */
	struct FBenchmarkSummary
	{
		double P50 = 0.0;           // latency P50 (ms)
		double P95 = 0.0;           // latency P95 (ms)
		double P99 = 0.0;           // latency P99 (ms)
		double AvgTps = 0.0;        // 초당 평균 수신 이동 횟수
		uint64 PeakTps = 0;         // 단일 초 최대 수신 이동 횟수
		uint64 TotalMoves = 0;      // 전체 수신 이동 횟수
		uint64 DropTotal = 0;       // 누적 drop 카운트
		double DropRate = 0.0;      // drop / (drop + recv)
		uint64 TimeoutTotal = 0;    // 누적 timeout 카운트
		double TimeoutRate = 0.0;   // timeout / (timeout + recv)
	};

	/**
	 * 전체 실행 동안의 측정 결과를 집계하고 CSV 로 저장한다.
	 *
	 * 설계: BotSession 마다 std::vector<FLatencySample> 을 들고 있고,
	 * 메인 루프가 kDrainTickMs 주기로 DrainSamples() 를 긁어와 여기에 Merge 한다.
	 * 스레드 안전성: Merge·MergeDropCount·MergeTimeoutCount 는 메인 스레드에서만 호출한다.
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
		 * 샘플 묶음을 해당 초 버킷에 병합한다.
		 *
		 * @param Samples  해당 drain 주기 동안 누적된 latency 샘플
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

		/** 봇의 drop 카운터를 소비해 누산한다. DrainSamples 와 같은 주기에서 호출한다. */
		void MergeDropCount(uint64 Count) { TotalDropCount += Count; }

		/** 봇의 timeout 카운터를 소비해 누산한다. DrainSamples 와 같은 주기에서 호출한다. */
		void MergeTimeoutCount(uint64 Count) { TotalTimeoutCount += Count; }

		/**
		 * 전체 구간 요약 통계를 stdout 에 출력하고 CSV 파일로 저장한다.
		 *
		 * @param CsvPath       결과 CSV 출력 경로
		 * @param DurationSec   테스트 총 실행 시간 (초) — AvgTps 분모
		 * @param SessionCount  동시 접속 봇 세션 수 — 메타 기록용
		 * @param MoveIntervalMs  C_MOVE 전송 주기 (밀리초) — 메타 기록용
		 */
		void Report(const std::string& CsvPath, uint32 DurationSec, uint32 SessionCount, uint32 MoveIntervalMs)
		{
			// 전체 latency 샘플 수집 및 TPS 집계.
			std::vector<double> AllLatencies;
			AllLatencies.reserve(1024);
			uint64 TotalMoves = 0;
			uint64 PeakPerSecond = 0;
			for (const FMetricsBucket& Bucket : Buckets)
			{
				TotalMoves += Bucket.MoveRecvCount; // 해당 버킷 초에 받은 S_MOVE 패킷 갯수
				if (Bucket.MoveRecvCount > PeakPerSecond)  // 가장 패킷을 많이 받은 시간초
					PeakPerSecond = Bucket.MoveRecvCount;
				for (double L : Bucket.LatenciesMs) // 해당 시간초에 저장된 RTT 샘플 배열들 
					AllLatencies.push_back(L);
			}

			// 전체 구간 요약 통계 계산.
			FBenchmarkSummary Summary = ComputeSummary(AllLatencies, TotalMoves, PeakPerSecond, DurationSec);

			// stdout 요약 출력.
			std::printf("[D1LoadBot] ==== Benchmark Summary ====\n");
			std::printf("[D1LoadBot] Sessions        : %u\n", SessionCount);
			std::printf("[D1LoadBot] MoveIntervalMs  : %u\n", MoveIntervalMs);
			std::printf("[D1LoadBot] DurationSec     : %u\n", DurationSec);
			std::printf("[D1LoadBot] TotalMoveRecv   : %llu\n", static_cast<unsigned long long>(Summary.TotalMoves));
			std::printf("[D1LoadBot] AvgTps          : %.2f\n", Summary.AvgTps);
			std::printf("[D1LoadBot] PeakTps         : %llu\n", static_cast<unsigned long long>(Summary.PeakTps));
			std::printf("[D1LoadBot] LatencyP50 (ms) : %.3f\n", Summary.P50);
			std::printf("[D1LoadBot] LatencyP95 (ms) : %.3f\n", Summary.P95);
			std::printf("[D1LoadBot] LatencyP99 (ms) : %.3f\n", Summary.P99);
			std::printf("[D1LoadBot] Samples         : %zu\n", AllLatencies.size());
			std::printf("[D1LoadBot] DropTotal       : %llu\n", static_cast<unsigned long long>(Summary.DropTotal));
			std::printf("[D1LoadBot] DropRate        : %.4f\n", Summary.DropRate);
			std::printf("[D1LoadBot] TimeoutTotal    : %llu\n", static_cast<unsigned long long>(Summary.TimeoutTotal));
			std::printf("[D1LoadBot] TimeoutRate     : %.4f\n", Summary.TimeoutRate);

			WriteCsv(CsvPath, Summary, DurationSec, SessionCount, MoveIntervalMs);
		}

	private:
		/** 샘플 시각을 StartTime 기준 초 인덱스로 변환한다. 시작 이전 샘플은 0 번 버킷에 넣는다. */
		uint32 BucketIndexFor(std::chrono::steady_clock::time_point At) const
		{
			const auto Delta = std::chrono::duration_cast<std::chrono::seconds>(At - StartTime).count();
			return (Delta < 0) ? 0u : static_cast<uint32>(Delta);
		}

		/** Idx 번 버킷이 없으면 생성하고 참조를 반환한다. */
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

		/**
		 * 전체 구간 요약 통계(Percentile, TPS, Drop/Timeout 비율)를 계산한다.
		 *
		 * AllLatencies 는 정렬을 위해 비-const 참조로 받는다 (Report 내 복사본을 전달할 것).
		 *
		 * @param AllLatencies   전체 RTT 샘플 (정렬됨)
		 * @param TotalMoves     전체 수신 이동 횟수
		 * @param PeakPerSecond  단일 초 최대 수신 횟수
		 * @param DurationSec    테스트 총 실행 시간 (초)
		 * @return               계산된 FBenchmarkSummary
		 */
		FBenchmarkSummary ComputeSummary(std::vector<double>& AllLatencies, uint64 TotalMoves, uint64 PeakPerSecond, uint32 DurationSec)
		{
			FBenchmarkSummary Summary;

			// Percentile 계산.
			Summary.P50 = Percentile(AllLatencies, kP50);
			Summary.P95 = Percentile(AllLatencies, kP95);
			Summary.P99 = Percentile(AllLatencies, kP99);

			// TPS 계산.
			Summary.AvgTps = (DurationSec > 0) ? (static_cast<double>(TotalMoves) / DurationSec) : 0.0;
			Summary.PeakTps = PeakPerSecond;
			Summary.TotalMoves = TotalMoves;

			// Drop 비율 계산.
			Summary.DropTotal = TotalDropCount;
			const double DropDenom = static_cast<double>(Summary.DropTotal + TotalMoves);
			Summary.DropRate = (DropDenom > 0.0) ? (static_cast<double>(Summary.DropTotal) / DropDenom) : 0.0;

			// Timeout 비율 계산.
			Summary.TimeoutTotal = TotalTimeoutCount;
			const double TimeoutDenom = static_cast<double>(Summary.TimeoutTotal + TotalMoves);
			Summary.TimeoutRate = (TimeoutDenom > 0.0) ? (static_cast<double>(Summary.TimeoutTotal) / TimeoutDenom) : 0.0;

			return Summary;
		}

		/**
		 * Values 의 Ratio 백분위수를 반환한다.
		 *
		 * Values 를 정렬하므로 호출자는 복사본을 전달해야 한다.
		 *
		 * @param Values  RTT 샘플 목록 (정렬 후 참조)
		 * @param Ratio   0.0~1.0 백분위 비율 (예: 0.95 = P95)
		 * @return        해당 백분위 값. 빈 배열이면 0.0.
		 */
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

		/**
		 * 집계 결과를 CSV 파일로 저장한다.
		 *
		 * 파일 구조: [메타 섹션] key-value 행 + [초당 TPS/latency 섹션] 시각화용 테이블.
		 *
		 * @param Path         출력 파일 경로
		 * @param Summary      ComputeSummary 가 반환한 요약 통계
		 * @param DurationSec  테스트 총 실행 시간 (초)
		 * @param SessionCount 동시 접속 봇 세션 수
		 * @param MoveIntervalMs C_MOVE 전송 주기 (밀리초)
		 */
		void WriteCsv(const std::string& Path, const FBenchmarkSummary& Summary, uint32 DurationSec, uint32 SessionCount, uint32 MoveIntervalMs)
		{
			std::ofstream File(Path);
			if (File.is_open() == false)
			{
				std::printf("[D1LoadBot] CSV open failed: %s\n", Path.c_str());
				return;
			}

			// 메타 헤더 — key-value 섹션으로 기록해 흔한 CSV 소비자와 호환을 유지한다.
			File << "section,key,value\n";
			File << "meta,sessions," << SessionCount << "\n";
			File << "meta,move_interval_ms," << MoveIntervalMs << "\n";
			File << "meta,duration_sec," << DurationSec << "\n";
			File << "meta,total_move_recv," << Summary.TotalMoves << "\n";
			File << "meta,avg_tps," << Summary.AvgTps << "\n";
			File << "meta,peak_tps," << Summary.PeakTps << "\n";
			File << "meta,latency_p50_ms," << Summary.P50 << "\n";
			File << "meta,latency_p95_ms," << Summary.P95 << "\n";
			File << "meta,latency_p99_ms," << Summary.P99 << "\n";
			File << "meta,drop_total," << Summary.DropTotal << "\n";
			File << "meta,drop_rate," << Summary.DropRate << "\n";
			File << "meta,timeout_total," << Summary.TimeoutTotal << "\n";
			File << "meta,timeout_rate," << Summary.TimeoutRate << "\n";

			// 초당 TPS/latency 샘플 — 시각화용 테이블.
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

		/** Merge 의 버킷 인덱스 기준 시각. Start() 호출 시 확정된다. */
		std::chrono::steady_clock::time_point StartTime{};

		/** 초 단위로 누적된 측정 버킷 목록. */
		std::vector<FMetricsBucket> Buckets;

		/** 전체 실행 동안 누산된 drop 카운트. */
		uint64 TotalDropCount = 0;

		/** 전체 실행 동안 누산된 timeout 카운트. */
		uint64 TotalTimeoutCount = 0;
	};
}