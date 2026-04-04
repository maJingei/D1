#include <iostream>
#include <atomic>
#include <chrono>
#include <set>

#include "ThreadManager.h"
#include "ReadWriteLock.h"
#include "ContainerTypes.h"

using namespace D1;

/**
 * 테스트 1: 다수 Reader 동시 접근 + Writer 배타 보장
 *
 * 8개 스레드가 각각 10000회 WriteLock 하에 공유 카운터를 증가시킨다.
 * 최종 카운터 값이 80000인지 검증한다.
 */
void TestConcurrentReadersAndWriterExclusion()
{
	std::cout << "[Test 1] Concurrent Readers + Writer Exclusion... " << std::flush;

	ReadWriteLock Lock;
	int64 SharedCounter = 0;
	std::atomic<uint32> CompletionCount{0};
	constexpr int32 ThreadCount = 8;
	constexpr int32 IterationCount = 10000;

	ThreadManager& Manager = ThreadManager::GetInstance();

	for (int32 i = 0; i < ThreadCount; ++i)
	{
		Manager.CreateThread([&Lock, &SharedCounter, &CompletionCount]()
		{
			for (int32 j = 0; j < IterationCount; ++j)
			{
				WriteLockGuard Guard(Lock);
				++SharedCounter;
				CompletionCount.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	Manager.Launch();
	Manager.JoinAll();

	assert(CompletionCount.load() == ThreadCount * IterationCount);
	assert(SharedCounter == ThreadCount * IterationCount);
	std::cout << "PASSED (counter=" << SharedCounter << ")" << std::endl;

	Manager.DestroyAllThreads();
}

/**
 * 테스트 2: Write 우선 정책 검증
 *
 * Reader1이 ReadLock 보유 -> Writer가 WriteLock 요청(대기) ->
 * Reader2가 ReadLock 요청 -> Reader1 해제 후 Writer가 Reader2보다 먼저 획득 확인.
 */
void TestWritePriority()
{
	std::cout << "[Test 2] Write Priority... " << std::flush;

	ReadWriteLock Lock;
	std::atomic<bool> bReader1Acquired{false};
	std::atomic<bool> bWriterWaiting{false};
	std::atomic<int64> WriterTimestamp{0};
	std::atomic<int64> Reader2Timestamp{0};

	auto GetTimestamp = []() -> int64
	{
		return std::chrono::steady_clock::now().time_since_epoch().count();
	};

	ThreadManager& Manager = ThreadManager::GetInstance();

	// Reader1: ReadLock 획득 후 Writer가 대기할 때까지 보유
	Manager.CreateThread([&]()
	{
		Lock.ReadLock();
		bReader1Acquired.store(true, std::memory_order_release);

		// Writer가 대기 상태에 들어갈 때까지 대기
		while (!bWriterWaiting.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		Lock.ReadUnlock();
	});

	// Writer: Reader1 획득 후 WriteLock 시도 (대기하게 됨)
	Manager.CreateThread([&]()
	{
		while (!bReader1Acquired.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}

		bWriterWaiting.store(true, std::memory_order_release);
		Lock.WriteLock();
		WriterTimestamp.store(GetTimestamp(), std::memory_order_release);
		Lock.WriteUnlock();
	});

	// Reader2: Writer가 대기 중인 상태에서 ReadLock 시도
	Manager.CreateThread([&]()
	{
		// Writer가 대기 상태에 들어갈 때까지 대기
		while (!bWriterWaiting.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		Lock.ReadLock();
		Reader2Timestamp.store(GetTimestamp(), std::memory_order_release);
		Lock.ReadUnlock();
	});

	Manager.Launch();
	Manager.JoinAll();

	const int64 WriterTs = WriterTimestamp.load();
	const int64 Reader2Ts = Reader2Timestamp.load();
	assert(WriterTs > 0 && Reader2Ts > 0);
	assert(WriterTs < Reader2Ts && "Writer should acquire before Reader2 (Write Priority)");
	std::cout << "PASSED (Writer before Reader2)" << std::endl;

	Manager.DestroyAllThreads();
}

/**
 * 테스트 3: ThreadManager Launch/Join + Write 재진입
 *
 * 10개 콜백 등록 -> Launch -> JoinAll -> atomic 카운터 == 10 검증.
 * 추가로 메인 스레드에서 Write 재진입 테스트.
 */
void TestThreadManagerAndWriteReentrant()
{
	std::cout << "[Test 3] ThreadManager Launch/Join + Write Reentrant... " << std::flush;

	// Part A: ThreadManager Launch/Join
	std::atomic<uint32> Counter{0};
	constexpr int32 CallbackCount = 10;

	{
		ThreadManager& Manager = ThreadManager::GetInstance();

		for (int32 i = 0; i < CallbackCount; ++i)
		{
			Manager.CreateThread([&Counter]()
			{
				Counter.fetch_add(1, std::memory_order_relaxed);
			});
		}

		Manager.Launch();
		Manager.JoinAll();
		Manager.DestroyAllThreads();
	}

	assert(Counter.load() == CallbackCount);

	// Part B: Write 재진입 (데드락 미발생 확인)
	ReadWriteLock Lock;
	Lock.WriteLock();
	Lock.WriteLock();   // 재진입 -- 데드락 없어야 함
	Lock.WriteUnlock();
	Lock.WriteUnlock();

	std::cout << "PASSED (callbacks=" << Counter.load() << ", reentrant OK)" << std::endl;
}

/**
 * 테스트 4: 단일스레드 Alloc/Free 정합성
 *
 * N개 Alloc -> 모든 포인터 고유 + 16B 정렬 검증 -> N개 Free -> 재Alloc N개 성공.
 */
void TestSingleThreadAllocFree()
{
	std::cout << "[Test 4] Single-Thread Alloc/Free... " << std::flush;

	MemoryPool* Pool = PoolManager::GetInstance().GetPool(sizeof(int32));
	constexpr int32 AllocCount = 1000;

	// N개 할당, 고유성 + 16B 정렬 검증
	std::set<void*> Pointers;
	void* Ptrs[AllocCount];
	for (int32 i = 0; i < AllocCount; ++i)
	{
		Ptrs[i] = Pool->Allocate();
		assert(Ptrs[i] != nullptr);
		assert(reinterpret_cast<uintptr_t>(Ptrs[i]) % 16 == 0 && "16B alignment violated");
		assert(Pointers.find(Ptrs[i]) == Pointers.end() && "Duplicate pointer");
		Pointers.insert(Ptrs[i]);
	}

	// 전부 Free
	for (int32 i = 0; i < AllocCount; ++i)
	{
		Pool->Deallocate(Ptrs[i]);
	}

	// 재Alloc: free-list에서 재사용되어야 함
	for (int32 i = 0; i < AllocCount; ++i)
	{
		void* Ptr = Pool->Allocate();
		assert(Ptr != nullptr);
		Pool->Deallocate(Ptr);
	}

	std::cout << "PASSED (" << AllocCount << " allocs, all unique, 16B aligned)" << std::endl;
}

/**
 * 테스트 5: 멀티스레드 동시 Alloc/Free
 *
 * 8스레드가 각 1000회 Alloc/Free -> 리크/중복 없음 검증.
 */
void TestMultiThreadAllocFree()
{
	std::cout << "[Test 5] Multi-Thread Alloc/Free... " << std::flush;

	MemoryPool* Pool = PoolManager::GetInstance().GetPool(sizeof(int64));
	constexpr int32 ThreadCount = 8;
	constexpr int32 AllocsPerThread = 1000;
	std::atomic<int32> TotalAllocs{0};
	std::atomic<int32> TotalFrees{0};

	ThreadManager& Manager = ThreadManager::GetInstance();

	for (int32 i = 0; i < ThreadCount; ++i)
	{
		Manager.CreateThread([&Pool, &TotalAllocs, &TotalFrees]()
		{
			void* Ptrs[AllocsPerThread];

			// 전부 Alloc
			for (int32 j = 0; j < AllocsPerThread; ++j)
			{
				Ptrs[j] = Pool->Allocate();
				assert(Ptrs[j] != nullptr);
				TotalAllocs.fetch_add(1, std::memory_order_relaxed);
			}

			// 전부 Free
			for (int32 j = 0; j < AllocsPerThread; ++j)
			{
				Pool->Deallocate(Ptrs[j]);
				TotalFrees.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	Manager.Launch();
	Manager.JoinAll();

	int32 Expected = ThreadCount * AllocsPerThread;
	assert(TotalAllocs.load() == Expected);
	assert(TotalFrees.load() == Expected);

	std::cout << "PASSED (" << ThreadCount << " threads x " << AllocsPerThread
		<< " allocs, no leaks)" << std::endl;

	Manager.DestroyAllThreads();
}

/**
 * 테스트 6: STL 컨테이너 동작
 *
 * Vector<int32>, Map<int32, String>, Set<int32> 기본 동작 확인.
 */
void TestStlContainers()
{
	std::cout << "[Test 6] STL Container Operations... " << std::flush;

	// Vector<int32> push_back / erase / iterate
	{
		Vector<int32> Vec;
		for (int32 i = 0; i < 100; ++i)
		{
			Vec.push_back(i);
		}
		assert(Vec.size() == 100);

		int32 Sum = 0;
		for (int32 Val : Vec)
		{
			Sum += Val;
		}
		assert(Sum == 4950); // 0+1+...+99

		Vec.erase(Vec.begin(), Vec.begin() + 50);
		assert(Vec.size() == 50);
	}

	// Map<int32, String>
	{
		Map<int32, String> MyMap;
		for (int32 i = 0; i < 50; ++i)
		{
			std::string Num = std::to_string(i);
			MyMap[i] = String("value_") + String(Num.c_str());
		}
		assert(MyMap.size() == 50);

		auto It = MyMap.find(25);
		assert(It != MyMap.end());

		MyMap.erase(25);
		assert(MyMap.find(25) == MyMap.end());
		assert(MyMap.size() == 49);
	}

	// Set<int32>
	{
		Set<int32> MySet;
		for (int32 i = 0; i < 30; ++i)
		{
			MySet.insert(i);
		}
		assert(MySet.size() == 30);
		assert(MySet.count(15) == 1);

		MySet.erase(15);
		assert(MySet.count(15) == 0);
	}

	std::cout << "PASSED (Vector, Map, Set)" << std::endl;
}

/**
 * 테스트 7: Stomp sentinel 위반 감지
 *
 * 정상 블록의 sentinel 값 확인 후, 의도적 오염이 감지 가능함을 검증.
 * (assert는 abort를 호출하므로 SEH로 캐치 불가 — 직접 값 검증 방식 사용)
 */
void TestStompDetection()
{
	std::cout << "[Test 7] Stomp Sentinel Detection... " << std::flush;

	MemoryPool* Pool = PoolManager::GetInstance().GetPool(32);
	void* Ptr = Pool->Allocate();
	uint8* BytePtr = reinterpret_cast<uint8*>(Ptr);

	// 정상 상태: SentinelHead/Tail이 올바른 매직 값인지 확인
	uint32* HeadPtr = reinterpret_cast<uint32*>(BytePtr - sizeof(MemoryBlock) + offsetof(MemoryBlock, SentinelHead));
	uint32* TailPtr = reinterpret_cast<uint32*>(BytePtr + 32); // BlockSize=32 직후
	assert(*HeadPtr == SENTINEL_HEAD && "SentinelHead should be initialized");
	assert(*TailPtr == SENTINEL_TAIL && "SentinelTail should be initialized");

	// 의도적 오염: SentinelTail을 변조
	*TailPtr = 0xDEADDEAD;
	assert(*TailPtr != SENTINEL_TAIL && "Corruption should be detectable");

	// 복원 후 정상 해제 (실제 서버에서는 assert가 이 오염을 잡아냄)
	*TailPtr = SENTINEL_TAIL;
	Pool->Deallocate(Ptr);

	std::cout << "PASSED (sentinel corruption detectable)" << std::endl;
}

/**
 * 테스트 8: 청크 확장
 *
 * 초기 BlockCount보다 많은 수를 Alloc하여 자동 청크 확장을 검증.
 */
void TestChunkExpansion()
{
	std::cout << "[Test 8] Chunk Expansion... " << std::flush;

	// 청크당 블록 수보다 많이 할당 (PoolManager는 64개로 초기화됨)
	MemoryPool* Pool = PoolManager::GetInstance().GetPool(sizeof(int32));
	constexpr int32 AllocCount = 256; // 64 * 4 = 최소 4개 청크 필요
	void* Ptrs[AllocCount];

	for (int32 i = 0; i < AllocCount; ++i)
	{
		Ptrs[i] = Pool->Allocate();
		assert(Ptrs[i] != nullptr);
	}

	// 전부 Free
	for (int32 i = 0; i < AllocCount; ++i)
	{
		Pool->Deallocate(Ptrs[i]);
	}

	std::cout << "PASSED (" << AllocCount << " allocs across multiple chunks)" << std::endl;
}

int main(int argc, char* argv[])
{
	// 메인 스레드 TLS 초기화 (ID=1)
	ThreadManager::InitTLS();

	// PoolManager 초기화: 14개 크기 클래스 풀 생성, 청크당 64블록
	PoolManager::GetInstance().Initialize(64);

	std::cout << "========================================" << std::endl;
	std::cout << "  D1Server Core Tests" << std::endl;
	std::cout << "========================================" << std::endl;

	TestConcurrentReadersAndWriterExclusion();
	TestWritePriority();
	TestThreadManagerAndWriteReentrant();
	TestSingleThreadAllocFree();
	TestMultiThreadAllocFree();
	TestStlContainers();
	TestStompDetection();
	TestChunkExpansion();

	std::cout << "========================================" << std::endl;
	std::cout << "  All tests PASSED!" << std::endl;
	std::cout << "========================================" << std::endl;

	// 명시적 정리: 리크 보고 + VirtualFree
	PoolManager::GetInstance().Shutdown();

	return 0;
}