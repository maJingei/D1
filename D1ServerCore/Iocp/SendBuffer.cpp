#include "SendBuffer.h"
#include "Memory/ObjectPool.h"
#include <cassert>

// 스레드 로컬 현재 Chunk.
// 파일 스코프로 둬서 SendBufferManager::ShutdownThread()가 명시적으로 reset할 수 있게 한다.
// 메인 스레드의 TLS는 CRT가 `return 0` 이후 소멸자를 호출하므로 PoolManager::Shutdown
// 이전에 미리 비워 주지 않으면 ObjectPool 커스텀 Deleter가 파괴된 PoolManager에 접근한다.
static thread_local SendBufferChunkRef LCurrentChunk;

/*-----------------------------------------------------------------*/
/*  SendBuffer                                                      */
/*-----------------------------------------------------------------*/

SendBuffer::SendBuffer(SendBufferChunkRef InOwner, uint8* InBuffer, int32 InAllocSize)
	: BufferPtr(InBuffer)
	, AllocSize(InAllocSize)
	, WriteSize(0)
	, Owner(std::move(InOwner))
{
}

void SendBuffer::Close(int32 WrittenSize)
{
	assert(WrittenSize >= 0 && WrittenSize <= AllocSize);

	WriteSize = WrittenSize;
	Owner->AdvanceCursor(WrittenSize);
}

/*-----------------------------------------------------------------*/
/*  SendBufferManager                                               */
/*-----------------------------------------------------------------*/

SendBufferManager& SendBufferManager::Get()
{
	static SendBufferManager Instance;
	return Instance;
}

SendBufferRef SendBufferManager::Open(int32 ReserveSize)
{
	assert(ReserveSize > 0 && ReserveSize <= SendBufferChunk::ChunkSize);

	// 현재 Chunk가 없거나 남은 공간 부족 → 새 Chunk 획득
	// 파일 스코프 thread_local이므로 락 없이 스레드별로 독립 동작한다.
	if (LCurrentChunk == nullptr || LCurrentChunk->GetFreeSize() < ReserveSize)
	{
		// ObjectPool에서 Chunk 획득. 참조 카운트 0이 되면 커스텀 Deleter가 풀로 반환한다.
		LCurrentChunk = ObjectPool<SendBufferChunk>::MakeShared();
		LCurrentChunk->Reset();
	}

	// 현재 cursor 위치에서 slice를 잘라 SendBuffer를 만든다.
	// cursor 전진은 SendBuffer::Close에서 수행되므로 여기서는 Chunk 상태를 바꾸지 않는다.
	uint8* SlicePtr = LCurrentChunk->BufferPtr();
	return std::make_shared<SendBuffer>(LCurrentChunk, SlicePtr, ReserveSize);
}

void SendBufferManager::ShutdownThread()
{
	// 이번 스레드의 LCurrentChunk 참조를 여기서 끊어준다.
	// 이 호출이 PoolManager::Shutdown보다 먼저 이뤄지면
	// Chunk deleter가 아직 살아있는 PoolManager로 안전하게 반환된다.
	LCurrentChunk.reset();
}
