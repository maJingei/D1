#include "MemoryPool.h"
#include <cstdio>

// ===================================================================
// MemoryPool
// ===================================================================

MemoryPool::MemoryPool(SizeType InBlockSize, uint32 InBlockCount)
	: ChunkHead(nullptr)
	, BlockSize(InBlockSize)
	, TotalBlockSize(0)
	, BlockCountPerChunk(InBlockCount)
	, AllocCount(0)
{
	InitializeSListHead(&FreeList);
	InitializeCriticalSection(&ChunkLock);
	TotalBlockSize = CalculateTotalBlockSize();
	AllocateChunk();
}

MemoryPool::~MemoryPool()
{
	int32 LeakCount = AllocCount.load(std::memory_order_relaxed);
	if (LeakCount != 0)
	{
		std::printf("[MemoryPool] LEAK DETECTED: %d blocks (BlockSize=%zu) not returned\n", LeakCount, BlockSize);
	}

	// MemoryChunk 자체가 VirtualAlloc 영역의 시작 주소이므로 직접 VirtualFree
	MemoryChunk* Current = ChunkHead;
	while (Current != nullptr)
	{
		MemoryChunk* Next = Current->NextChunk;
		VirtualFree(Current, 0, MEM_RELEASE);
		Current = Next;
	}
	ChunkHead = nullptr;

	DeleteCriticalSection(&ChunkLock);
}

SizeType MemoryPool::CalculateTotalBlockSize() const
{
	/**
	 * 블록 메모리 레이아웃:
	 *   Offset 0:            SLIST_ENTRY  (16B) -- lock-free 리스트 노드
	 *   Offset 16:           SentinelHead (4B) + Padding (12B) = 16B
	 *   Offset 32:           UserData     (BlockSize bytes)
	 *   Offset 32+BlockSize: SentinelTail (4B)
	 */
	SizeType Raw = sizeof(SLIST_ENTRY) + 16 + BlockSize + sizeof(uint32);
	return (Raw + 15) & ~static_cast<SizeType>(15); // 16바이트 정렬
}

void MemoryPool::AllocateChunk()
{
	// 청크 확장은 cold path — CS로 직렬화하여 중복 VirtualAlloc 최소화
	EnterCriticalSection(&ChunkLock);

	SizeType ChunkHeaderSize = (sizeof(MemoryChunk) + 15) & ~static_cast<SizeType>(15);
	SizeType TotalChunkSize = ChunkHeaderSize + TotalBlockSize * BlockCountPerChunk;

	void* RawMemory = VirtualAlloc(nullptr, TotalChunkSize,MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	assert(RawMemory != nullptr && "VirtualAlloc failed");

	// MemoryChunk를 VirtualAlloc 영역 시작에 carve (별도 new 불필요)
	MemoryChunk* NewChunk = reinterpret_cast<MemoryChunk*>(RawMemory);
	NewChunk->ChunkSize = TotalChunkSize;
	NewChunk->NextChunk = ChunkHead;
	ChunkHead = NewChunk;

	// ChunkHeader 직후부터 블록 배열 — 각 블록을 SLIST에 Push
	uint8* BlockStart = reinterpret_cast<uint8*>(RawMemory) + ChunkHeaderSize;
	for (uint32 i = 0; i < BlockCountPerChunk; ++i)
	{
		MemoryBlock* Block = reinterpret_cast<MemoryBlock*>(BlockStart + TotalBlockSize * i);
		InterlockedPushEntrySList(&FreeList, &Block->SListEntry);
	}

	LeaveCriticalSection(&ChunkLock);
}

void* MemoryPool::Allocate()
{
	// Retry loop: Pop 실패(free-list 고갈) 시 청크 확장 후 재시도
	while (true)
	{
		// SLIST Pop은 lock-free (x64 128-bit CAS, ABA-safe)
		MemoryBlock* Block = reinterpret_cast<MemoryBlock*>(InterlockedPopEntrySList(&FreeList));

		if (Block != nullptr)
		{
			InitializeSentinels(Block);
			AllocCount.fetch_add(1, std::memory_order_relaxed);
			return GetUserPtrFromBlock(Block);
		}

		AllocateChunk();
	}
}

void MemoryPool::Deallocate(void* Ptr)
{
	MemoryBlock* Block = GetBlockFromUserPtr(Ptr);
	ValidateSentinels(Block);

	/**
	 * Double-free 감지: 해제된 블록은 SENTINEL_FREE로 마킹.
	 * 멀티스레드 한계: A free -> B alloc(리셋) -> A double-free 시 미감지.
	 * 이는 lock-free pool의 본질적 한계.
	 */
	assert(Block->SentinelHead != SENTINEL_FREE && "Double-free detected");
	Block->SentinelHead = SENTINEL_FREE;

	InterlockedPushEntrySList(&FreeList, &Block->SListEntry);
	AllocCount.fetch_sub(1, std::memory_order_relaxed);
}

SizeType MemoryPool::GetBlockSize() const
{
	return BlockSize;
}

MemoryBlock* MemoryPool::GetBlockFromUserPtr(void* UserPtr) const
{
	// UserPtr에서 sizeof(MemoryBlock)(=32B) 역산하여 블록 헤더 획득
	return reinterpret_cast<MemoryBlock*>(reinterpret_cast<uint8*>(UserPtr) - sizeof(MemoryBlock));
}

void* MemoryPool::GetUserPtrFromBlock(MemoryBlock* Block) const
{
	// 블록 시작 + 32B(헤더) = UserData 시작 (16B 정렬 보장)
	return reinterpret_cast<uint8*>(Block) + sizeof(MemoryBlock);
}

void MemoryPool::InitializeSentinels(MemoryBlock* Block)
{
	Block->SentinelHead = SENTINEL_HEAD;
	uint32* Tail = GetSentinelTailPtr(Block);
	*Tail = SENTINEL_TAIL;
}

void MemoryPool::ValidateSentinels(MemoryBlock* Block) const
{
	assert(Block->SentinelHead == SENTINEL_HEAD && "Memory corruption: SentinelHead overwritten");

	uint32* Tail = GetSentinelTailPtr(Block);
	assert(*Tail == SENTINEL_TAIL && "Memory corruption: SentinelTail overwritten (buffer overflow)");
}

uint32* MemoryPool::GetSentinelTailPtr(MemoryBlock* Block) const
{
	// SentinelTail = UserData 영역 직후 4바이트
	uint8* UserData = reinterpret_cast<uint8*>(Block) + sizeof(MemoryBlock);
	return reinterpret_cast<uint32*>(UserData + BlockSize);
}

// ===================================================================
// PoolManager
// ===================================================================

PoolManager& PoolManager::GetInstance()
{
	// Leaked singleton: 정적 소멸 순서 문제 회피, process exit 시 OS가 회수
	static PoolManager* Instance = new PoolManager();
	return *Instance;
}

uint32 PoolManager::GetSizeClassIndex(SizeType Size)
{
	if (Size <= 8)
	{
		return 0;
	}

	/**
	 * _BitScanReverse64(Size-1)로 O(1) 크기 클래스 인덱스 계산.
	 * (Size-1)을 사용하여 정확한 2의 거듭제곱일 때 같은 클래스에 매핑.
	 *
	 *   Size=9  -> BSR(8)=3  -> 3-2=1 -> SIZE_CLASSES[1]=16
	 *   Size=16 -> BSR(15)=3 -> 3-2=1 -> SIZE_CLASSES[1]=16
	 *   Size=17 -> BSR(16)=4 -> 4-2=2 -> SIZE_CLASSES[2]=32
	 *   Size=65 -> BSR(64)=6 -> 6-2=4 -> SIZE_CLASSES[4]=128
	 */
	unsigned long Index = 0;
	_BitScanReverse64(&Index, Size - 1);

	// SIZE_CLASSES는 2^3(=8)부터이므로 BSR 결과에서 2를 빼면 배열 인덱스
	uint32 ClassIndex = static_cast<uint32>(Index) - 2;

	if (ClassIndex >= NUM_SIZE_CLASSES)
	{
		ClassIndex = NUM_SIZE_CLASSES - 1;
	}
	return ClassIndex;
}

void PoolManager::Initialize(uint32 BlockCountPerChunk)
{
	// 14개 크기 클래스(8B~64KB) 풀을 일괄 생성
	for (uint32 i = 0; i < NUM_SIZE_CLASSES; ++i)
	{
		assert(Pools[i] == nullptr && "PoolManager already initialized");
		Pools[i] = new MemoryPool(SIZE_CLASSES[i], BlockCountPerChunk);
	}
}

MemoryPool* PoolManager::GetPool(SizeType Size)
{
	if (Size == 0)
	{
		Size = 1;
	}

	// MAX_POOL_SIZE 초과는 풀 대상이 아님 — 호출자가 LargeAllocate로 라우팅
	if (Size > MAX_POOL_SIZE)
	{
		return nullptr;
	}

	uint32 Index = GetSizeClassIndex(Size);
	assert(Pools[Index] != nullptr && "PoolManager not initialized");
	return Pools[Index];
}

void PoolManager::Shutdown()
{
	// delete nullptr은 no-op이므로 이중 Shutdown 안전
	for (uint32 i = 0; i < NUM_SIZE_CLASSES; ++i)
	{
		delete Pools[i];
		Pools[i] = nullptr;
	}
}

PoolManager::~PoolManager()
{
	Shutdown();
}

// ===================================================================
// Large Allocation (65536 초과)
// ===================================================================

void* LargeAllocate(SizeType Size)
{
	void* Ptr = VirtualAlloc(nullptr, Size,MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	assert(Ptr != nullptr && "VirtualAlloc failed for large allocation");
	return Ptr;
}

void LargeDeallocate(void* Ptr)
{
	if (Ptr != nullptr)
	{
		VirtualFree(Ptr, 0, MEM_RELEASE);
	}
}
