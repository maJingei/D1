#include "TimeManager.h"

TimeManager& TimeManager::Get()
{
	// Meyers singleton: 프로세스 종료 시 dtor 자동 호출.
	static TimeManager Instance;
	return Instance;
}

TimeManager::~TimeManager()
{
	Shutdown();
}

void TimeManager::Initialize()
{
	::QueryPerformanceFrequency(&Frequency);
	::QueryPerformanceCounter(&PrevTime);
	CurrentTime = PrevTime;
	DeltaTime = 0.0f;
	TotalTime = 0.0f;
	FrameCount = 0;
	FPSTimer = 0.0f;
	CurrentFPS = 0.0f;
}

void TimeManager::Update()
{
	::QueryPerformanceCounter(&CurrentTime);
	DeltaTime = static_cast<float>(CurrentTime.QuadPart - PrevTime.QuadPart) / static_cast<float>(Frequency.QuadPart);
	PrevTime = CurrentTime;
	TotalTime += DeltaTime;
	// FPS 계산: 1초마다 갱신
	FrameCount++;
	FPSTimer += DeltaTime;
	if (FPSTimer >= 1.0f)
	{
		CurrentFPS = static_cast<float>(FrameCount) / FPSTimer;
		FrameCount = 0;
		FPSTimer = 0.0f;
	}
}

void TimeManager::Shutdown()
{
}
