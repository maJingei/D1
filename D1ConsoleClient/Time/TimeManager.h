#pragma once

#include "Core/CoreMinimal.h"

#include "Core/CoreMinimal.h"

/** 프레임 시간 관리 싱글톤. */
class TimeManager
{
public:
	static TimeManager& Get();

	/** 타이머를 초기화한다. QueryPerformanceFrequency로 주파수 획득. */
	void Initialize();

	/** 매 프레임 호출. DeltaTime 계산 및 FPS 갱신. */
	void Update();

	/** 타이머를 정리한다. */
	void Shutdown();

	float GetDeltaTime() const { return DeltaTime; }
	float GetTotalTime() const { return TotalTime; }
	float GetFPS() const { return CurrentFPS; }

private:
	TimeManager() = default;
	~TimeManager();
	TimeManager(const TimeManager&) = delete;
	TimeManager& operator=(const TimeManager&) = delete;

	LARGE_INTEGER Frequency = {};
	LARGE_INTEGER PrevTime = {};
	LARGE_INTEGER CurrentTime = {};
	float DeltaTime = 0.0f;
	float TotalTime = 0.0f;
	uint32 FrameCount = 0;
	float FPSTimer = 0.0f;
	float CurrentFPS = 0.0f;
};
