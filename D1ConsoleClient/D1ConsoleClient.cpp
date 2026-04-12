#include <Windows.h>

#include "Game/Game.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"

using namespace D1;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);

	if (Game::Get().Initialize(hInstance))
	{
		Game::Get().Run();
	}

	Game::Get().Shutdown();
	PoolManager::GetInstance().Shutdown();

	return 0;
}
