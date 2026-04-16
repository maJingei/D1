#pragma once

#include <vector>
#include "Core/CoreMinimal.h"

/**
 * TCP 수신 스트림을 누적하는 커서 기반 버퍼.
 *
 * [0 ... ReadPos ... WritePos ... Capacity]
 *        |-- unread --|-- free --|
 *
 * 사용 패턴:
 *  1) RegisterRecv 직전에 Clean()을 호출하여 여유 공간 확보
 *  2) WSARecv의 WSABUF에 WritePtr()/GetFreeSize() 지정
 *  3) 수신 완료 후 OnWrite(NumOfBytes)로 WritePos 전진
 *  4) 파생 OnRecv에서 ReadPtr() ~ ReadPtr()+GetDataSize() 범위를 파싱
 *  5) 처리 완료 바이트 수만큼 OnRead(Processed)로 ReadPos 전진
 *
 * 공간 재활용 정책:
 *  - ReadPos == WritePos → 둘 다 0으로 리셋 (가장 흔한 경우)
 *  - 잔여 데이터 존재 → 앞으로 memmove하여 뒤쪽 공간 확보
 *
 * 단일 스레드(IOCP 워커)에서 직렬 접근되므로 동기화 없음.
 */
class RecvBuffer
{
public:
	/** 기본 용량 64KB (스펙 합의값). */
	static constexpr int32 DefaultCapacity = 65536;

	explicit RecvBuffer(int32 InCapacity = DefaultCapacity);

	/** RegisterRecv 직전 호출: 쓰기 공간 확보. */
	void Clean();

	/** ReadPos를 NumOfBytes만큼 전진시킨다. 범위 초과 시 false. */
	bool OnRead(int32 NumOfBytes);

	/** WritePos를 NumOfBytes만큼 전진시킨다. 범위 초과 시 false. */
	bool OnWrite(int32 NumOfBytes);

	/** 현재 미처리 데이터의 시작 포인터. */
	uint8* ReadPtr() { return &Buffer[ReadPos]; }

	/** 다음 수신이 기록될 포인터. */
	uint8* WritePtr() { return &Buffer[WritePos]; }

	/** 아직 파싱되지 않은 누적 바이트 수. */
	int32 GetDataSize() const { return WritePos - ReadPos; }

	/** 이번 WSARecv가 최대 받을 수 있는 바이트 수. */
	int32 GetFreeSize() const { return Capacity - WritePos; }

	int32 GetCapacity() const { return Capacity; }

private:
	int32 Capacity = 0;
	int32 ReadPos  = 0;
	int32 WritePos = 0;
	std::vector<uint8> Buffer;
};
