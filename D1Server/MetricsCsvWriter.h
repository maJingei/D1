// LOG LOGIC FILE : 서버 측 처리량/부하 측정 CSV 모듈. 측정 시스템 제거 시 이 파일 통째 삭제.
#pragma once

#include <cstdint>
#include <string>

/**
 * 서버 측 처리량/부하 측정 CSV 라이터.
 *
 * 사용 시퀀스:
 *   1. D1Server 시작 시 Initialize(bEnabled, "server_metrics.csv", RoomCount) 1회.
 *   2. 첫 세션 OnConnected 시점에 TryStartTimeline() — bucket 0 = 이 시각으로 CAS 1회만.
 *   3. reporter thread 가 1초마다 Tick() — DiagCounters delta 를 bucket 에 적재.
 *   4. 서버 종료 시 Flush() — Buckets 를 csv 로 직렬화.
 *
 * Initialize(false, ...) 면 모든 함수가 no-op (atomic load 1회만, 사실상 0 오버헤드).
 */
namespace MetricsCsvWriter
{
	// LOG LOGIC : 모듈 활성화 + 출력 경로 + 룸 수 설정. D1Server.cpp 에서 1회 호출.
	void Initialize(bool bInEnabled, std::string InOutputPath, uint32_t InRoomCount);

	// LOG LOGIC : 첫 세션 진입 시각을 bucket 0 으로 CAS 설정. 다중 호출되어도 첫 호출만 효과.
	void TryStartTimeline();

	// LOG LOGIC : 1초 주기로 reporter thread 에서 호출. 누계 카운터 delta → bucket 추가.
	void Tick();

	// LOG LOGIC : 종료 시 1회 호출. Buckets 를 csv 로 flush.
	void Flush();
}
