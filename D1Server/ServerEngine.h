#pragma once

#include "Core/CoreMinimal.h"

#include <atomic>
#include <thread>

/** 서버 전체 수명 주기를 관리하는 엔진 클래스. */
class ServerEngine
{
public:
	ServerEngine() = default;
	~ServerEngine() = default;

	ServerEngine(const ServerEngine&) = delete;
	ServerEngine& operator=(const ServerEngine&) = delete;

	/** 서브시스템 초기화 (Winsock, PoolManager, ThreadManager TLS, World 등). */
	bool Init();

	/** World::BeginPlay 호출 후 TimerLoop 스레드를 시작한다. */
	void BeginPlay();

	/** TimerLoop 중단 → World::Destroy → 싱글톤 정리. */
	void Destroy();

private:
	/** 20ms 주기로 각 Level 의 JobQueue 에 TickJob 을 push 한다. */
	void TimerLoop();

	std::atomic<bool> bRunning{false};
	std::thread TimerThread;
};
