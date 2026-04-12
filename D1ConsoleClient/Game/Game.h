#pragma once

#include <Windows.h>

#include "Core/Types.h"

namespace D1
{
	/**
	 * 게임 클라이언트의 메인 싱글톤.
	 * 윈도우 생성, 게임 루프, 서브시스템 관리를 담당한다.
	 */
	class Game
	{
	public:
		static Game& Get();

		/**
		 * 게임을 초기화한다. 윈도우 생성 및 서브시스템 초기화.
		 *
		 * @param hInstance  WinMain에서 받은 인스턴스 핸들
		 * @return           초기화 성공 여부
		 */
		bool Initialize(HINSTANCE hInstance);

		/** BeginPlay → 게임 루프(PeekMessage + Tick) → EndPlay */
		void Run();

		/** 서브시스템 해제 및 윈도우 파괴 */
		void Shutdown();

	private:
		Game() = default;
		~Game() = default;
		Game(const Game&) = delete;
		Game& operator=(const Game&) = delete;

		void BeginPlay();
		void Tick(float DeltaTime);
		void EndPlay();

		bool CreateGameWindow();
		void UpdateWindowTitle();

		static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

		HINSTANCE HInstance = nullptr;
		HWND HWnd = nullptr;
		bool bIsRunning = false;

		static constexpr int32 WindowWidth = 1280;
		static constexpr int32 WindowHeight = 720;
	};
}
