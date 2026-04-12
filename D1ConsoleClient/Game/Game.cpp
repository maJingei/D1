#include "Game.h"

#include "../Subsystems/TimeManager.h"
#include "../Subsystems/InputManager.h"
#include "../Subsystems/Renderer.h"

#include <cstdio>

namespace D1
{
	Game& Game::Get()
	{
		static Game* Instance = new Game();
		return *Instance;
	}

	bool Game::Initialize(HINSTANCE hInstance)
	{
		HInstance = hInstance;
		if (!CreateGameWindow())
		{
			return false;
		}
		// 서브시스템 초기화 (순서 중요: TimeManager → InputManager → Renderer)
		TimeManager::Get().Initialize();
		InputManager::Get().Initialize();
		if (!Renderer::Get().Initialize(HWnd, WindowWidth, WindowHeight))
		{
			return false;
		}
		return true;
	}

	void Game::Run()
	{
		bIsRunning = true;
		BeginPlay();
		while (bIsRunning)
		{
			MSG Msg = {};
			while (::PeekMessage(&Msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (Msg.message == WM_QUIT)
				{
					bIsRunning = false;
					break;
				}
				::TranslateMessage(&Msg);
				::DispatchMessage(&Msg);
			}
			if (!bIsRunning)
			{
				break;
			}
			TimeManager::Get().Update();
			InputManager::Get().Update();
			// ESC 키로 게임 종료
			if (InputManager::Get().GetKeyDown(VK_ESCAPE))
			{
				bIsRunning = false;
				break;
			}
			Tick(TimeManager::Get().GetDeltaTime());
			Renderer::Get().BeginRender();
			// 향후 게임 오브젝트 렌더링이 여기에 추가됨
			Renderer::Get().EndRender();
			UpdateWindowTitle();
		}
		EndPlay();
	}

	void Game::Shutdown()
	{
		// 역순 해제: Renderer → InputManager → TimeManager → Window
		Renderer::Get().Shutdown();
		InputManager::Get().Shutdown();
		TimeManager::Get().Shutdown();
		if (HWnd)
		{
			::DestroyWindow(HWnd);
			HWnd = nullptr;
		}
		::UnregisterClass(L"D1GameWindow", HInstance);
	}

	void Game::BeginPlay()
	{
	}

	void Game::Tick(float DeltaTime)
	{
	}

	void Game::EndPlay()
	{
	}

	bool Game::CreateGameWindow()
	{
		WNDCLASSEX WndClass = {};
		WndClass.cbSize = sizeof(WNDCLASSEX);
		WndClass.style = CS_HREDRAW | CS_VREDRAW;
		WndClass.lpfnWndProc = WndProc;
		WndClass.hInstance = HInstance;
		WndClass.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		WndClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
		WndClass.lpszClassName = L"D1GameWindow";
		if (!::RegisterClassEx(&WndClass))
		{
			return false;
		}
		// 클라이언트 영역이 정확히 WindowWidth x WindowHeight가 되도록 조정
		RECT WindowRect = { 0, 0, WindowWidth, WindowHeight };
		::AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);
		int32 AdjustedWidth = WindowRect.right - WindowRect.left;
		int32 AdjustedHeight = WindowRect.bottom - WindowRect.top;
		HWnd = ::CreateWindowEx(
			0,
			L"D1GameWindow",
			L"D1 Game Client",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			AdjustedWidth, AdjustedHeight,
			nullptr, nullptr, HInstance, nullptr
		);
		if (!HWnd)
		{
			return false;
		}
		::ShowWindow(HWnd, SW_SHOW);
		::UpdateWindow(HWnd);
		return true;
	}

	void Game::UpdateWindowTitle()
	{
		float FPS = TimeManager::Get().GetFPS();
		float DT = TimeManager::Get().GetDeltaTime();
		wchar_t Title[256] = {};
		::swprintf_s(Title, L"D1 Game Client | FPS: %.1f | DT: %.4f", FPS, DT);
		::SetWindowText(HWnd, Title);
	}

	LRESULT CALLBACK Game::WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
	{
		switch (Msg)
		{
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
		}
		return ::DefWindowProc(hWnd, Msg, wParam, lParam);
	}
}
