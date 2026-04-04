#include "ThreadManager.h"

namespace D1
{
	std::atomic<uint32> ThreadManager::ThreadIdCounter{1};
	thread_local uint32 ThreadManager::CurrentThreadId = INVALID_THREAD_ID;

	ThreadManager& ThreadManager::GetInstance()
	{
		// Leaked singleton: 정적 소멸 순서 문제 회피, process exit 시 OS가 회수
		static ThreadManager* Instance = new ThreadManager();
		return *Instance;
	}

	ThreadManager::~ThreadManager()
	{
		DestroyAllThreads();
	}

	void ThreadManager::InitTLS()
	{
		CurrentThreadId = ThreadIdCounter.fetch_add(1);
		assert(CurrentThreadId <= MAX_THREAD_ID && "ThreadID exceeds 15-bit limit (max 32767)");
	}

	uint32 ThreadManager::GetCurrentThreadId()
	{
		return CurrentThreadId;
	}

	void ThreadManager::CreateThread(std::function<void()> Callback)
	{
		Callbacks.push_back(std::move(Callback));
	}

	void ThreadManager::Launch()
	{
		for (auto& Callback : Callbacks)
		{
			Threads.emplace_back([Func = std::move(Callback)]()
			{
				InitTLS();
				Func();
			});
		}
		Callbacks.clear();
	}

	void ThreadManager::JoinAll()
	{
		for (auto& Thread : Threads)
		{
			if (Thread.joinable())
			{
				Thread.join();
			}
		}
	}

	void ThreadManager::DestroyAllThreads()
	{
		JoinAll();
		Threads.clear();
		Callbacks.clear();
	}
}