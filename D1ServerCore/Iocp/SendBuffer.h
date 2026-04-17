#pragma once

#include <array>
#include <memory>
#include "Core/CoreMinimal.h"

class SendBuffer;

/** SendBuffer들이 slice로 공유하는 8KB 고정 크기 청크. */
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

	/** SendBuffer::Close에서 호출: 실제로 기록된 크기만큼 cursor 전진. */
	void AdvanceCursor(int32 WrittenSize) { UsedSize += WrittenSize; }

	SendBufferChunk(const SendBufferChunk&) = delete;
	SendBufferChunk& operator=(const SendBufferChunk&) = delete;

private:
	std::array<uint8, ChunkSize> Buffer{};
	int32 UsedSize = 0;
};

/** 패킷 하나를 담는 불변 공유 객체. */
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

	/** 기록 완료 확정. */
	void Close(int32 WrittenSize);

	SendBuffer(const SendBuffer&) = delete;
	SendBuffer& operator=(const SendBuffer&) = delete;

private:
	uint8* BufferPtr = nullptr;
	int32 AllocSize = 0;
	int32 WriteSize = 0;
	SendBufferChunkRef Owner;
};

/** 스레드 로컬 기반 SendBuffer 할당자. */
class SendBufferManager
{
public:
	static SendBufferManager& Get();

	/** 주어진 크기의 slice SendBuffer를 예약한다. */
	SendBufferRef Open(int32 ReserveSize);

	/** 현재 스레드의 TLS Chunk 참조를 명시적으로 해제한다. */
	static void ShutdownThread();

private:
	SendBufferManager() = default;
};
