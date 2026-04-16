#include "ReadWriteLock.h"
#include "ThreadManager.h"

void ReadWriteLock::WriteLock()
{
	const uint32 CurrentId = ThreadManager::GetCurrentThreadId();
	assert(CurrentId != INVALID_THREAD_ID && "ThreadManager::InitTLS() must be called before WriteLock");
	assert(CurrentId <= MAX_THREAD_ID && "ThreadID exceeds 15-bit limit");

	// 재진입 확인: Owner가 현재 스레드와 일치하면 Count(=WCount)만 증가
	const uint32 Current = LockState.load(std::memory_order_acquire);
	const uint32 Owner = (Current & OWNER_MASK) >> OWNER_SHIFT;

	if (Owner == CurrentId)
	{
		assert((Current & COUNT_MASK) < COUNT_MASK && "Write reentrant count overflow");
		LockState.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	// 신규 획득: W + Owner + Count=1 을 원자적으로 CAS
	const uint32 Desired = WRITE_FLAG | (CurrentId << OWNER_SHIFT) | 1;

	// Write 우선 정책: 대기 중임을 알림
	WriteWaiters.fetch_add(1, std::memory_order_relaxed);

	while (true)
	{
		// Phase 1: Load-Check-CAS (TTAS) 스핀
		for (uint32 SpinIndex = 0; SpinIndex < SPIN_COUNT; ++SpinIndex)
		{
			// 가벼운 load로 먼저 확인 (캐시 라인 무효화 없음)
			if (LockState.load(std::memory_order_relaxed) != 0)
			{
				continue;
			}

			// LockState == 0 확인 후 CAS 시도
			uint32 Expected = 0;
			if (LockState.compare_exchange_weak(
				Expected,
				Desired,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				WriteWaiters.fetch_sub(1, std::memory_order_relaxed);
				return;
			}
		}

		// Phase 2: WaitOnAddress sleep
		uint32 Snapshot = LockState.load(std::memory_order_acquire);
		if (Snapshot != 0)
		{
			WaitOnAddress(
				&LockState,
				&Snapshot,
				sizeof(uint32),
				INFINITE);
		}
	}
}

void ReadWriteLock::WriteUnlock()
{
	const uint32 CurrentId = ThreadManager::GetCurrentThreadId();
	const uint32 Current = LockState.load(std::memory_order_acquire);
	const uint32 Owner = (Current & OWNER_MASK) >> OWNER_SHIFT;
	assert(Owner == CurrentId && "WriteUnlock called by non-owner thread");

	const uint32 WriteCount = Current & COUNT_MASK;
	assert(WriteCount > 0 && "WriteUnlock called but WriteCount is 0");

	if (WriteCount == 1)
	{
		// 마지막 해제: 전체 클리어
		LockState.store(0, std::memory_order_release);
		WakeByAddressAll(&LockState);
	}
	else
	{
		// 재진입 카운트 감소
		LockState.fetch_sub(1, std::memory_order_release);
	}
}

void ReadWriteLock::ReadLock()
{
	while (true)
	{
		// Phase 1: 짧은 스핀
		for (uint32 SpinIndex = 0; SpinIndex < SPIN_COUNT; ++SpinIndex)
		{
			uint32 Current = LockState.load(std::memory_order_acquire);

			// Write 우선 정책: Writer 보유 중이거나 대기 중이면 스핀
			if ((Current & WRITE_FLAG) || WriteWaiters.load(std::memory_order_relaxed) > 0)
			{
				continue;
			}

			// W=0이므로 Count = ReadCount
			assert((Current & COUNT_MASK) < COUNT_MASK && "Read count overflow");

			uint32 Desired = Current + 1;
			if (LockState.compare_exchange_weak(
				Current, Desired,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				return;
			}
		}

		// Phase 2: 조건별 대기
		uint32 Snapshot = LockState.load(std::memory_order_acquire);
		if (Snapshot & WRITE_FLAG)
		{
			// Writer가 락을 보유 중 → LockState 변경 시 깨어남
			WaitOnAddress(
				&LockState,
				&Snapshot,
				sizeof(uint32),
				INFINITE);
		}
		else if (WriteWaiters.load(std::memory_order_relaxed) > 0)
		{
			// Writer가 대기 중이지만 아직 미획득 → yield 후 재시도
			// WaitOnAddress 사용 불가 (LockState=0이면 영구 블록 위험)
			std::this_thread::yield();
		}
	}
}

void ReadWriteLock::ReadUnlock()
{
	const uint32 Prev = LockState.fetch_sub(1, std::memory_order_release);
	assert((Prev & COUNT_MASK) > 0 && "ReadUnlock underflow");

	// 마지막 Reader 해제 시 대기 중인 Writer를 깨움
	if ((Prev & COUNT_MASK) == 1)
	{
		WakeByAddressAll(&LockState);
	}
}
