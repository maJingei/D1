#pragma once

#include "../Core/Types.h"

namespace D1
{
	/** Stomp 감지용 센티넬 매직 값 */
	constexpr uint32 SENTINEL_HEAD = 0xDEADBEEF;
	constexpr uint32 SENTINEL_TAIL = 0xBAADF00D;
	constexpr uint32 SENTINEL_FREE = 0xFEEEFEEE;

	/** 크기 클래스 정의 (14개: 8B ~ 64KB, 2의 거듭제곱) */
	constexpr SizeType SIZE_CLASSES[] = {
		8, 16, 32, 64, 128, 256, 512, 1024,
		2048, 4096, 8192, 16384, 32768, 65536
	};
	constexpr uint32 NUM_SIZE_CLASSES = 14;
	constexpr SizeType MAX_POOL_SIZE = 65536;

	/**
	 * 메모리 블록 레이아웃 (사용자 데이터 16바이트 정렬):
	 *
	 * [SLIST_ENTRY (16B)][SentinelHead (4B) + Padding (12B) = 16B][UserData (BlockSize)][SentinelTail (4B)][Padding]
	 *
	 * UserData는 offset 32에서 시작 -> 16바이트 정렬 보장.
	 */
	struct MemoryBlock
	{
		SLIST_ENTRY SListEntry;     // 16 bytes (offset 0)
		uint32 SentinelHead;        // 4 bytes  (offset 16)
		uint8 Padding[12];          // 12 bytes (offset 20) -- UserData 16B 정렬용
		// UserData는 offset 32부터 BlockSize 바이트
		// SentinelTail은 UserData 직후 4바이트
	};

	/**
	 * VirtualAlloc으로 할당된 메모리 청크.
	 * VirtualAlloc 영역의 시작 부분에 직접 배치된다 (별도 new/delete 불필요).
	 */
	struct MemoryChunk
	{
		MemoryChunk* NextChunk;
		SizeType ChunkSize;
	};

	/**
	 * 고정 크기 블록을 관리하는 메모리 풀.
	 * SLIST 기반 lock-free free-list로 단일 블록 할당/해제.
	 * 풀 고갈 시 새 VirtualAlloc 청크로 자동 확장.
	 */
	class MemoryPool
	{
	public:
		/**
		 * 메모리 풀을 생성한다.
		 *
		 * @param InBlockSize        사용자 데이터 크기 (bytes)
		 * @param InBlockCount       청크당 초기 블록 수
		 */
		MemoryPool(SizeType InBlockSize, uint32 InBlockCount);
		~MemoryPool();

		MemoryPool(const MemoryPool&) = delete;
		MemoryPool& operator=(const MemoryPool&) = delete;

		/** 블록 1개를 할당한다. lock-free (SLIST Pop). Pop 실패 시 청크 확장 후 재시도. */
		void* Allocate();

		/** 블록 1개를 반환한다. Stomp sentinel 검증 후 SLIST Push. */
		void Deallocate(void* Ptr);

		/** 풀의 블록 크기를 반환한다. */
		SizeType GetBlockSize() const;

	private:
		/** 새 청크를 VirtualAlloc으로 할당하고 free-list에 등록한다. */
		void AllocateChunk();

		/**
		 * 블록 전체 크기 (헤더 + sentinel + padding + 데이터 + sentinel + 패딩, 16B 정렬).
		 * Raw = sizeof(SLIST_ENTRY) + 16 + BlockSize + sizeof(uint32)
		 * TotalBlockSize = (Raw + 15) & ~15
		 */
		SizeType CalculateTotalBlockSize() const;

		/** 사용자 데이터 포인터에서 MemoryBlock 헤더를 역산한다. */
		MemoryBlock* GetBlockFromUserPtr(void* UserPtr) const;

		/** MemoryBlock에서 사용자 데이터 포인터를 계산한다 (offset 32). */
		void* GetUserPtrFromBlock(MemoryBlock* Block) const;

		/** 블록의 Stomp sentinel을 초기화한다. */
		void InitializeSentinels(MemoryBlock* Block);

		/** 블록의 Stomp sentinel을 검증한다. */
		void ValidateSentinels(MemoryBlock* Block) const;

		/** 블록의 SentinelTail 포인터를 반환한다. */
		uint32* GetSentinelTailPtr(MemoryBlock* Block) const;

		alignas(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER FreeList;
		MemoryChunk* ChunkHead;
		SizeType BlockSize;
		SizeType TotalBlockSize;
		uint32 BlockCountPerChunk;
		std::atomic<int32> AllocCount;
		CRITICAL_SECTION ChunkLock;
	};

	static_assert(alignof(MemoryPool) >= MEMORY_ALLOCATION_ALIGNMENT,
		"MemoryPool must satisfy SLIST_HEADER alignment requirement");

	/**
	 * 크기 클래스별 MemoryPool을 관리하는 글로벌 싱글턴.
	 * 서버 초기화 시 14개 크기 클래스 풀을 일괄 생성하고,
	 * 런타임에는 읽기 전용 O(1) 조회만 수행한다.
	 *
	 * Leaked singleton 패턴 사용: process exit 시 OS가 메모리 회수.
	 * 명시적 Shutdown()으로 리크 보고 및 정리 가능.
	 */
	class PoolManager
	{
	public:
		/** Leaked singleton: new로 생성, process exit로 회수. */
		static PoolManager& GetInstance();

		/**
		 * 14개 크기 클래스 풀을 일괄 생성한다. 서버 초기화 시 1회 호출.
		 *
		 * @param BlockCountPerChunk  크기 클래스당 청크 내 초기 블록 수
		 */
		void Initialize(uint32 BlockCountPerChunk);

		/**
		 * 요청 크기에 맞는 풀을 O(1)로 조회한다.
		 * _BitScanReverse64로 가장 가까운 상위 2의 거듭제곱 크기 클래스 인덱스를 계산.
		 *
		 * @param Size  요청 크기 (bytes). MAX_POOL_SIZE 초과 시 nullptr 반환.
		 */
		MemoryPool* GetPool(SizeType Size);

		/**
		 * 명시적 종료. 리크 보고 + 모든 풀/청크 VirtualFree.
		 * 서버 종료 시 호출한다.
		 */
		void Shutdown();

		~PoolManager();

	private:
		PoolManager() = default;
		PoolManager(const PoolManager&) = delete;
		PoolManager& operator=(const PoolManager&) = delete;

		/** 요청 크기에서 크기 클래스 인덱스를 계산한다 (_BitScanReverse64). */
		static uint32 GetSizeClassIndex(SizeType Size);

		MemoryPool* Pools[NUM_SIZE_CLASSES] = {};
	};

	/** 65536 초과 대형 할당. 직접 VirtualAlloc. */
	void* LargeAllocate(SizeType Size);

	/** 대형 할당 해제. 직접 VirtualFree. */
	void LargeDeallocate(void* Ptr);
}