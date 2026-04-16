#include "Core/CoreMinimal.h"

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "Game.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Iocp/SocketUtils.h"
#include "Iocp/SendBuffer.h"
#include "Core/DiagCounters.h"
#include "Job/GlobalJobQueue.h"
#include "Job/JobQueue.h"

#include <atomic>
#include <thread>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	// Debug 빌드 누수 체크: 프로세스 종료 시 CRT가 미반환 힙을 출력창에 리포트한다.
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);
	SocketUtils::Init();

	// Flush Worker — Session::Send 가 Serializer Job 으로 Push 되므로 소비 주체 필수.
	// 클라이언트는 싱글 세션이라 1개면 충분 (경합 거의 없음). 없으면 WSASend 가
	// 영원히 발사되지 않아 서버에 어떤 패킷도 도달하지 않는다.
	std::atomic<bool> bFlushWorkerRun{ true };
	std::thread FlushWorkerThread([&bFlushWorkerRun]()
	{
		ThreadManager::InitTLS();
		while (true)
		{
			JobQueueRef Queue = GlobalJobQueue::GetInstance().Pop(bFlushWorkerRun);
			if (Queue == nullptr)
				break;
			Queue->FlushJob();
		}
		// Flush Worker 도 자체 TLS SendBufferChunk 를 보유할 수 있으므로 명시적으로 반환한다.
		SendBufferManager::ShutdownThread();
	});

	// Game 은 스택 객체로 둔다. 정적 싱글톤으로 만들면 정적 dtor 호출 순서가
	// 서브시스템(Renderer/ResourceManager 등)보다 늦어 ~Game()→Shutdown() 시점에
	// 이미 파괴된 서브시스템에 접근해 크래시한다. 스택에 두면 이 블록 종료 시점에
	// ~Game() 이 호출되며 그때 서브시스템들의 정적 dtor 는 아직 실행되지 않은 상태다.
	{
		Game GameInstance;
		if (GameInstance.Initialize(hInstance))
		{
			GameInstance.Run();
			GameInstance.Shutdown();
		}
		// Initialize 실패 시에는 Game::Initialize 내부에서 부분 초기화 롤백 완료.
		// 정상/실패 모두 ~Game() 이 안전망으로 Shutdown 을 한 번 더 호출(멱등).
	}

	// Flush Worker 종료: Game Shutdown 이후 더 이상 Job Push 가 없으므로
	// 플래그를 내리고 WakeAll 로 Pop 대기 를 해제한다.
	bFlushWorkerRun.store(false, std::memory_order_relaxed);
	GlobalJobQueue::GetInstance().WakeAll();
	if (FlushWorkerThread.joinable()) FlushWorkerThread.join();

	SocketUtils::Cleanup();

	// 메인 스레드 TLS SendBufferChunk를 명시적으로 반환한다.
	// (CRT TLS 소멸자는 PoolManager::Shutdown 이후에 실행되므로 여기서 미리 비운다.)
	SendBufferManager::ShutdownThread();

	PoolManager::GetInstance().Shutdown();

	return 0;
}
