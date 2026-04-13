#pragma once

// Windows.h가 구버전 winsock.h를 끌어와 winsock2.h와 충돌하는 것을 방지한다.
// (NetAddress/SocketUtils 등이 winsock2를 요구하므로 이 헤더가 어디서 include되든 안전해야 함)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <memory>
#include <mutex>
#include <string>

#include "Core/Types.h"

namespace D1
{
	class ClientService;
	class Session;
	class UWorld;

	/**
	 * 게임 클라이언트의 메인 싱글톤.
	 * 윈도우 생성, 게임 루프, 서브시스템 관리를 담당한다.
	 */
	class Game
	{
	public:
		static Game& Get();

		/** Client Session의 OnRecv에서 호출: 수신 텍스트를 저장해 타이틀에 반영한다. */
		void OnEchoReceived(const uint8* Data, int32 NumOfBytes);

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
		~Game();
		Game(const Game&) = delete;
		Game& operator=(const Game&) = delete;

		void BeginPlay();
		void Tick(float DeltaTime);
		void EndPlay();

		/** 리소스를 로드하고 월드에 타일 레이어를 등록한다. BeginPlay에서 호출한다. */
		void LoadResources();

		bool CreateGameWindow();
		void UpdateWindowTitle();

		static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

		HINSTANCE HInstance = nullptr;
		HWND HWnd = nullptr;
		bool bIsRunning = false;

		/*-----------------------------------------------------------------*/
		/*  네트워크                                                        */
		/*-----------------------------------------------------------------*/

		ClientServiceRef Network;
		SessionRef ClientSession;

		/** 서버로부터 Echo 받은 마지막 문자열. Tick에서 타이틀에 반영된다. */
		std::mutex EchoMutex;
		std::string LastEcho;

		/** 게임 오브젝트를 소유하고 Tick/Render를 위임하는 월드. 맵 교체 시 재할당된다. */
		std::unique_ptr<UWorld> World;

		static constexpr int32 WindowWidth = 960;   // 30 타일 * 32px
		static constexpr int32 WindowHeight = 672;  // 21 타일 * 32px
	};
}
