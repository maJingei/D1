#pragma once

#include "../Core/Types.h"

namespace D1
{
	/**
	 * 하이브리드 CAS + WaitOnAddress 기반 ReadWriteLock.
	 *
	 * 32비트 LockState 하나로 Owner와 Count를 원자적으로 관리한다.
	 *   [W:1][ThreadID:15][Count:16]
	 *   - W=0: Count = ReadCount (동시 Reader 수)
	 *   - W=1: Count = WriteCount (재진입 깊이), ThreadID = 소유자
	 *
	 * Write 우선 정책: WriteWaiters 카운터로 대기 Writer 존재 시 새 Reader 차단.
	 * Write만 재진입을 지원한다.
	 *
	 * 락 전략: 짧은 스핀(SPIN_COUNT회) -> WaitOnAddress sleep -> WakeByAddressAll wake
	 */
	class ReadWriteLock
	{
	public:
		ReadWriteLock() = default;
		~ReadWriteLock() = default;

		ReadWriteLock(const ReadWriteLock&) = delete;
		ReadWriteLock& operator=(const ReadWriteLock&) = delete;
		ReadWriteLock(ReadWriteLock&&) = delete;
		ReadWriteLock& operator=(ReadWriteLock&&) = delete;

		/** Write 락을 획득한다. Write 우선 정책 적용. 재진입 지원. */
		void WriteLock();

		/** Write 락을 해제한다. 재진입 카운트가 0이 되면 대기자를 깨운다. */
		void WriteUnlock();

		/** Read 락을 획득한다. Writer 대기/보유 중이면 차단된다. */
		void ReadLock();

		/** Read 락을 해제한다. 마지막 Reader이면 대기 중인 Writer를 깨운다. */
		void ReadUnlock();

	private:
		/** [W:1][ThreadID:15][Count:16] */
		std::atomic<uint32> LockState{0};

		/** 대기 중인 Writer 수. Write 우선 정책에 사용. */
		std::atomic<uint32> WriteWaiters{0};
	};

	/** ReadLock RAII 가드. 생성 시 ReadLock, 소멸 시 ReadUnlock. */
	class ReadLockGuard
	{
	public:
		explicit ReadLockGuard(ReadWriteLock& InLock) : Lock(InLock)
		{
			Lock.ReadLock();
		}

		~ReadLockGuard()
		{
			Lock.ReadUnlock();
		}

		ReadLockGuard(const ReadLockGuard&) = delete;
		ReadLockGuard& operator=(const ReadLockGuard&) = delete;

	private:
		ReadWriteLock& Lock;
	};

	/** WriteLock RAII 가드. 생성 시 WriteLock, 소멸 시 WriteUnlock. */
	class WriteLockGuard
	{
	public:
		explicit WriteLockGuard(ReadWriteLock& InLock) : Lock(InLock)
		{
			Lock.WriteLock();
		}

		~WriteLockGuard()
		{
			Lock.WriteUnlock();
		}

		WriteLockGuard(const WriteLockGuard&) = delete;
		WriteLockGuard& operator=(const WriteLockGuard&) = delete;

	private:
		ReadWriteLock& Lock;
	};
}