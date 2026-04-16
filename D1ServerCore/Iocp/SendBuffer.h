#pragma once

#include <array>
#include <memory>
#include "Core/CoreMinimal.h"

class SendBuffer;

/**
 * SendBuffer들이 slice로 공유하는 8KB 고정 크기 청크.
 *
 * SendBufferManager가 TLS로 스레드당 하나의 Chunk를 유지하며,
 * Open(size)로 슬라이스를 잘라주고 SendBuffer::Close(writeSize)로 cursor를 전진시킨다.
 * 모든 slice SendBuffer가 소멸되면 Chunk도 함께 소멸하여 ObjectPool로 반환된다.
 *
 * 이 클래스 자체는 동기화 없음 (각 스레드 TLS로 독립 사용).
 */
class SendBufferChunk
{
public:
	static constexpr int32 ChunkSize = 8192;

	SendBufferChunk() = default;

	/** 재사용 전 호출: cursor 초기화. (ObjectPool에서 꺼내 올 때 상태 보장) */
	void Reset() { UsedSize = 0; }

	/** 다음 slice가 시작될 포인터. */
	uint8* BufferPtr() { return &Buffer[UsedSize]; }

	/** 현재 남은 여유 공간 크기. */
	int32 GetFreeSize() const { return ChunkSize - UsedSize; }

	/**
	 * SendBuffer::Close에서 호출: 실제로 기록된 크기만큼 cursor 전진.
	 * 예약된 크기보다 작게 기록된 경우 빈 공간은 다음 Open에서 재사용된다.
	 */
	void AdvanceCursor(int32 WrittenSize) { UsedSize += WrittenSize; }

	SendBufferChunk(const SendBufferChunk&) = delete;
	SendBufferChunk& operator=(const SendBufferChunk&) = delete;

private:
	std::array<uint8, ChunkSize> Buffer{};
	int32 UsedSize = 0;
};

/**
 * 패킷 하나를 담는 불변 공유 객체. SendBufferChunk의 slice를 참조한다.
 *
 * 사용 패턴:
 *   auto Buf = SendBufferManager::Get().Open(SomeSize);
 *   // Buf->Buffer()에 실제 데이터 기록 (SomeSize 이하)
 *   Buf->Close(ActualWrittenSize);
 *   Session->Send(Buf);
 *
 * Close 후에는 불변 객체로 간주되어 여러 Session이 shared_ptr로 공유할 수 있다.
 * (브로드캐스트 시 메모리 절약)
 */
class SendBuffer
{
public:
	SendBuffer(SendBufferChunkRef InOwner, uint8* InBuffer, int32 InAllocSize);
	~SendBuffer() = default;

	/** 실제 데이터를 기록할 버퍼 시작 포인터. (예약 크기 이내로만 기록) */
	uint8* Buffer() const { return BufferPtr; }

	/** Open 시 예약된 크기. */
	int32 GetAllocSize() const { return AllocSize; }

	/** Close로 확정된 실제 기록 크기. Close 전에는 0. */
	int32 GetWriteSize() const { return WriteSize; }

	/**
	 * 기록 완료 확정. Chunk의 cursor를 WrittenSize만큼 전진시킨다.
	 * 반드시 한 번만 호출되어야 하며, WrittenSize는 AllocSize 이하여야 한다.
	 */
	void Close(int32 WrittenSize);

	SendBuffer(const SendBuffer&) = delete;
	SendBuffer& operator=(const SendBuffer&) = delete;

private:
	uint8* BufferPtr = nullptr;
	int32 AllocSize = 0;
	int32 WriteSize = 0;
	SendBufferChunkRef Owner;
};

/**
 * 스레드 로컬 기반 SendBuffer 할당자. 싱글턴으로 접근한다.
 *
 * 각 스레드는 전용 SendBufferChunk를 소유하며, Open(size) 호출 시:
 *  - 현재 Chunk가 없거나 공간이 부족하면 ObjectPool에서 새 Chunk 획득
 *  - 현재 cursor 위치에서 size만큼 slice를 잘라 SendBuffer 반환
 *  - 실제 cursor 전진은 SendBuffer::Close에서 이루어짐 (실사용 크기 기준)
 *
 * Chunk의 모든 slice SendBuffer가 소멸하고 TLS 참조도 해제되면
 * Chunk는 ObjectPool로 자동 반환된다 (MakeShared의 커스텀 Deleter).
 *
 * TODO: 브로드캐스트 최적화를 위한 Chunk 풀 warm-up / 대형 메시지용 전용 경로.
 */
class SendBufferManager
{
public:
	static SendBufferManager& Get();

	/**
	 * 주어진 크기의 slice SendBuffer를 예약한다.
	 *
	 * @param ReserveSize  예약할 최대 기록 크기 (바이트). ChunkSize 이하여야 한다.
	 * @return             사용 가능한 SendBuffer (소유권 shared_ptr)
	 */
	SendBufferRef Open(int32 ReserveSize);

	/**
	 * 현재 스레드의 TLS Chunk 참조를 명시적으로 해제한다.
	 *
	 * 메인 스레드의 thread_local은 프로세스 종료 시(CRT가 TLS 소멸자를 호출할 때)
	 * PoolManager::Shutdown보다 나중에 소멸되므로, 그 시점엔 ObjectPool 경로가
	 * 파괴된 PoolManager에 접근하여 assert/UAF가 발생한다.
	 *
	 * 호출자는 PoolManager::Shutdown 직전에 반드시 이 함수를 호출해야 한다.
	 * 워커 스레드는 join 시점에 TLS가 먼저 소멸되므로 자동으로 안전하다.
	 */
	static void ShutdownThread();

private:
	SendBufferManager() = default;
};
