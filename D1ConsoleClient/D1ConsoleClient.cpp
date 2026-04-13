#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "Game/Game.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Iocp/SocketUtils.h"
#include "Iocp/SendBuffer.h"

using namespace D1;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	// Debug 빌드 누수 체크: 프로세스 종료 시 CRT가 미반환 힙을 출력창에 리포트한다.
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);
	SocketUtils::Init();

	if (Game::Get().Initialize(hInstance))
	{
		Game::Get().Run();
	}

	Game::Get().Shutdown();
	SocketUtils::Cleanup();

	// 메인 스레드 TLS SendBufferChunk를 명시적으로 반환한다.
	// (CRT TLS 소멸자는 PoolManager::Shutdown 이후에 실행되므로 여기서 미리 비운다.)
	SendBufferManager::ShutdownThread();

	PoolManager::GetInstance().Shutdown();

	return 0;
}
