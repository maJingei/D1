#pragma once

// Windows.h가 구버전 winsock.h를 끌어와 winsock2.h와 충돌하는 것을 방지한다.
// (NetAddress/SocketUtils 등이 winsock2를 요구하므로 이 헤더가 어디서 include되든 안전해야 함)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <memory>
#include <deque>
#include <string>

#include "Core/Types.h"

namespace D1
{
	class ClientService;
	class Session;
	class UWorld;

	/**
	 * 게임 클라이언트의 메인 객체.
	 * WinMain 이 스택에 1개를 들고 윈도우 생성·게임 루프·서브시스템 수명을 관리한다.
	 *
	 * 스택 객체로 둔 이유:
	 *  - 정적 싱글톤(Game) → 정적 싱글톤(Renderer/ResourceManager 등) 의 destruction 순서가
	 *    역순이라, ~Game() 의 Shutdown 안전망이 이미 파괴된 서브시스템에 접근해 크래시한다.
	 *  - 스택에 두면 WinMain 종료 시점에 서브시스템들의 정적 dtor 보다 먼저 ~Game() 이 호출돼
	 *    안전하게 Shutdown 을 수행할 수 있다.
	 */
	class Game
	{
	public:
		// 생성자/소멸자는 cpp 에 정의 — std::unique_ptr<UWorld> 의 incomplete type 문제 회피.
		Game();
		~Game();
		Game(const Game&) = delete;
		Game& operator=(const Game&) = delete;

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

		/** 현재 World 포인터. Packet Handler에서 수신한 Spawn 패킷을 월드에 반영하기 위해 공개. */
		UWorld* GetWorld() const { return World.get(); }

		/** 서버가 발급한 본인 PlayerID. S_ENTER_GAME 수신 시 설정된다. */
		uint64 GetMyPlayerID() const { return MyPlayerID; }
		void SetMyPlayerID(uint64 InPlayerID) { MyPlayerID = InPlayerID; }

		/** 전역 Game 인스턴스 접근자. Packet Handler에서 World에 접근하기 위한 통로. */
		static Game* GetInstance() { return Instance; }

		/** 화면 좌상단에 출력할 디버그 로그 한 줄을 큐에 추가한다. 오래된 줄은 자동 폐기된다. */
		void AddDebugLog(const wchar_t* Line);

		/** 서버에 연결된 세션. Packet 송신이 필요한 액터가 직접 접근한다. 미연결이면 nullptr. */
		const SessionRef& GetClientSession() const { return ClientSession; }

	private:

		void BeginPlay();
		void Tick(float DeltaTime);
		void EndPlay();

		/** 리소스를 로드하고 월드에 타일 레이어를 등록한다. BeginPlay에서 호출한다. */
		void LoadResources();

		bool CreateGameWindow();

		static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

		HINSTANCE HInstance = nullptr;
		HWND HWnd = nullptr;
		bool bIsRunning = false;

		/*-----------------------------------------------------------------*/
		/*  네트워크                                                        */
		/*-----------------------------------------------------------------*/

		ClientServiceRef Network;
		SessionRef ClientSession;

		/** 서버가 발급한 본인 PlayerID. S_ENTER_GAME 응답에서 받는다. */
		uint64 MyPlayerID = 0;

		/** 게임 오브젝트를 소유하고 Tick/Render를 위임하는 월드. 맵 교체 시 재할당된다. */
		std::unique_ptr<UWorld> World;

		/** 전역 접근용 정적 포인터. Initialize에서 this 등록, Shutdown에서 nullptr 복원. */
		static Game* Instance;

		/** 화면 좌상단에 오버레이로 뿌릴 최근 디버그 로그 큐. 가장 위가 가장 오래된 줄. */
		std::deque<std::wstring> DebugLogs;

		static constexpr int32 WindowWidth = 960;   // 30 타일 * 32px
		static constexpr int32 WindowHeight = 672;  // 21 타일 * 32px

		/** 화면에 동시에 유지할 디버그 로그 최대 줄 수. 초과분은 앞에서 폐기. */
		static constexpr int32 MaxDebugLogLines = 12;

		/**
		 * 게임 루프 목표 프레임율. Run() 에서 프레임 말미에 남는 시간만큼 sleep 으로 대기해
		 * CPU 를 전부 태우는 것을 막는다. 멀티박스 테스트(동일 PC 에서 여러 클라 동시 실행) 시
		 * 풀-스핀으로 인한 컨텍스트 스위칭 폭주를 억제하는 것이 주 목적이다.
		 */
		static constexpr int32 TargetFPS = 60;
	};
}