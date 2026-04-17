#pragma once

#include "Core/CoreMinimal.h"

/** 스레드 생성/관리 및 TLS ThreadID를 관리하는 매니저 클래스. */
class ThreadManager
{
public:
	/** Leaked singleton: new로 생성, process exit로 회수. */
	static ThreadManager& GetInstance();

	~ThreadManager();

	ThreadManager(const ThreadManager&) = delete;
	ThreadManager& operator=(const ThreadManager&) = delete;
	ThreadManager(ThreadManager&&) = delete;
	ThreadManager& operator=(ThreadManager&&) = delete;

	/** 현재 스레드의 TLS ThreadID를 초기화한다. 스레드 시작 시 반드시 호출. */
	static void InitTLS();

	/** 현재 스레드의 TLS ThreadID를 반환한다. InitTLS 미호출 시 INVALID_THREAD_ID 반환. */
	static uint32 GetCurrentThreadId();

	/** 콜백을 등록한다. */
	void CreateThread(std::function<void()> Callback);

	/** 등록된 모든 콜백에 대해 스레드를 생성하고 실행한다. */
	void Launch();

	/** 실행 중인 모든 스레드를 Join한다. */
	void JoinAll();

	/** 모든 스레드를 Join하고 내부 자원을 정리한다. */
	void DestroyAllThreads();

private:
	ThreadManager() = default;

	std::vector<std::thread> Threads;
	std::vector<std::function<void()>> Callbacks;

	static std::atomic<uint32> ThreadIdCounter;
	static thread_local uint32 CurrentThreadId;
};
