#pragma once

#include "Core/CoreMinimal.h"

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

	/** World::BeginPlay 호출. */
	void BeginPlay();

	/** 20ms 주기로 World::Tick 을 돌리는 메인 루프. 콘솔 키 입력 감지 시 복귀한다. */
	void Tick();

	/** World::Destroy → 싱글톤 정리. */
	void Destroy();
};
