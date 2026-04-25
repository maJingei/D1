#include "Game.h"

#include <chrono>
#include <thread>

#include "World/UWorld.h"
#include "World/UTileMap.h"
#include "UCollisionMap.h"
#include "GameObject/APlayerActor.h"
#include "GameObject/AMonsterActor.h"

#include "Time/TimeManager.h"
#include "Input/InputManager.h"
#include "Render/Renderer.h"
#include "Render/ResourceManager.h"

#include "Network/ClientService.h"
#include "Network/ServerPacketHandler.h"
#include "Network/GameClientSession.h"
#include "Iocp/NetAddress.h"
#include "Iocp/IocpCore.h"

#include "UI/ULoginWidget.h"
#include "UI/UChatPanel.h"

#include "Iocp/Session.h"
#include "Protocol.pb.h"

#include <string>

// 전역 Game 접근점. Packet Handler가 World/Debug 버퍼에 접근하기 위한 통로.
Game* Game::Instance = nullptr;

namespace
{
	/** UTF-16(GDI/wstring) → UTF-8(protobuf string). 빈 문자열은 빈 std::string 반환. */
	std::string WideToUtf8(const std::wstring& Wide)
	{
		if (Wide.empty()) return std::string();
		const int32 RequiredLen = ::WideCharToMultiByte(CP_UTF8, 0, Wide.data(), static_cast<int32>(Wide.size()), nullptr, 0, nullptr, nullptr);
		if (RequiredLen <= 0) return std::string();
		std::string Result(static_cast<size_t>(RequiredLen), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, Wide.data(), static_cast<int32>(Wide.size()), Result.data(), RequiredLen, nullptr, nullptr);
		return Result;
	}

#ifdef STRESS_DEBUG_HOOKS
	// S2 재현 훅: 대용량 echo 완료 직후 SO_LINGER={1,0} + closesocket로 RST 유도.
	// 본격 활용 시 키보드 핫키(F7 등)에서 호출.
	void TestRstAfterEcho(Session& /*S*/)
	{
		// TODO(S5 분리 PR): SO_LINGER 설정 + closesocket 호출 스텁.
	}

	// S3 재현 훅: RegisterConnect 직후 외부 Ref를 즉시 drop하여 in-flight ConnectEx를 고립시킨다.
	void TestConnectDrop(SessionRef& Ref)
	{
		Ref.reset();
	}

	// S4 재현 훅: F10 핫키로 외부 트리거 ServerService::Stop() 유도 (서버 프로세스 내 호출용).
	void TestAcceptDropHotkey()
	{
		// TODO(S5 분리 PR): 서버 Side 훅. 본 함수는 symbol 존재 확인용 스텁.
	}
#endif
}

// 생성자/소멸자는 cpp 에 정의해야 std::unique_ptr<UWorld> 의 incomplete type 문제를 회피한다.
Game::Game() = default;

// 스택 객체이므로 WinMain 종료 시 자동 호출되며,
// 이 시점에 정적 서브시스템(Renderer/ResourceManager 등)들은 아직 살아있어 안전.
// Shutdown 은 멱등이므로 WinMain 에서 명시적으로 호출한 뒤에도 무해하다.
Game::~Game()
{
	Shutdown();
}

void Game::Shutdown()
{
	// 역순 해제: ResourceManager → Renderer → InputManager → TimeManager → Window
	ResourceManager::Get().Shutdown();
	Renderer::Get().Shutdown();
	InputManager::Get().Shutdown();
	TimeManager::Get().Shutdown();
	if (HWnd)
	{
		::DestroyWindow(HWnd);
		HWnd = nullptr;
	}
	::UnregisterClass(L"D1GameWindow", HInstance);

	// 전역 Instance 해제 — Shutdown 이후 Packet Handler 등에서 stale 참조로 크래시하지 않도록 막는다.
	if (Instance == this)
		Instance = nullptr;
}

bool Game::Initialize(HINSTANCE hInstance)
{
	// 정적 Instance 포인터를 먼저 고정한다. 이후 서브시스템 초기화 중 콜백에서 Game::GetInstance() 사용 가능.
	Instance = this;

	HInstance = hInstance;
	if (!CreateGameWindow())
	{
		Instance = nullptr;
		return false;
	}
	// 서브시스템 초기화 (순서 중요: TimeManager → InputManager → Renderer → ResourceManager).
	// Renderer 초기화 실패 시 이미 초기화된 서브시스템과 윈도우를 역순으로 해제한다.
	TimeManager::Get().Initialize();
	InputManager::Get().Initialize();
	if (!Renderer::Get().Initialize(HWnd, WindowWidth, WindowHeight))
	{
		InputManager::Get().Shutdown();
		TimeManager::Get().Shutdown();
		::DestroyWindow(HWnd);
		HWnd = nullptr;
		::UnregisterClass(L"D1GameWindow", HInstance);
		return false;
	}
	ResourceManager::Get().Initialize();
	return true;
}

void Game::Run()
{
	bIsRunning = true;

	// 게임 로직 시작 전 초기화
	BeginPlay();

	// 프레임 캡 — TargetFPS 역수로 프레임당 허용 시간을 산출.
	// steady_clock 기반으로 매 프레임 시작/종료를 재고, 남는 시간만큼만 sleep 한다.
	const auto TargetFrameTime = std::chrono::microseconds(1'000'000 / TargetFPS);

	while (bIsRunning)
	{
		const auto FrameStart = std::chrono::steady_clock::now();

		// 반드시 PeekMessage 루프 '이전' 에 호출해야 한다.
		// PeekMessage 가 OnKeyDown 으로 CurrentKeyStates 를 갱신한 직후 Update 를 호출하면
		// 새 입력이 즉시 Previous 에도 true 로 복사되어 GetKeyDown 이 영원히 false 가 된다(에지 소실).
		InputManager::Get().Update();

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

		// 2. 시간 업데이트
		TimeManager::Get().Update();
		
		// ESC — 채팅 입력 활성 중에는 채팅 취소가 우선, 그 외에는 게임 종료.
		if (InputManager::Get().GetKeyDown(EKey::Escape))
		{
			if (ChatPanel != nullptr && ChatPanel->IsInputActive())
			{
				ChatPanel->CancelInput();
			}
			else
			{
				bIsRunning = false;
				break;
			}
		}
		
		// 3. 게임 로직 업데이트
		Tick(TimeManager::Get().GetDeltaTime());
		
		// 4. 렌더 진행 — Scene 분기. Login 상태에서는 World 가 완전히 그려지지 않고 ULoginWidget 만.
		Renderer::Get().BeginRender();
		if (CurrentState == EGameState::InGame)
		{
			if (World != nullptr)
				World->Render(Renderer::Get().GetBackBufferDC());

			// 채팅 패널 — World 위에 좌하단 오버레이로 합성. 패널 좌상단 좌표는 화면 크기 기준으로 산출.
			if (ChatPanel != nullptr)
			{
				constexpr int32 ChatPanelMargin = 10;
				const int32 ChatX = ChatPanelMargin;
				const int32 ChatY = WindowHeight - UChatPanel::PanelHeight - ChatPanelMargin;
				ChatPanel->Render(Renderer::Get().GetBackBufferDC(), ChatX, ChatY);
			}
		}
		else // EGameState::Login
		{
			if (LoginWidget != nullptr)
				LoginWidget->Render(Renderer::Get().GetBackBufferDC(), 0, 0);
		}
		// 디버그 오버레이 — 월드 위에, 화면 합성 전에 그린다.
		{
			constexpr int32 OverlayLeft = 8;
			constexpr int32 OverlayTop  = 8;
			constexpr int32 LineHeight  = 14;
			int32 LineIndex = 0;
			for (const std::wstring& Line : DebugLogs)
			{
				Renderer::Get().DrawDebugText(OverlayLeft, OverlayTop + LineIndex * LineHeight, Line.c_str());
				++LineIndex;
			}
		}
		Renderer::Get().EndRender();

		// 프레임 말미 — 목표 프레임 시간에서 실제 소요 시간을 뺀 만큼 sleep.
		// 일이 빨리 끝나면 쉬고, 느리면 바로 다음 프레임으로 넘어간다(스킵 아님 — 단순히 대기 생략).
		const auto Elapsed = std::chrono::steady_clock::now() - FrameStart;
		if (Elapsed < TargetFrameTime)
			std::this_thread::sleep_for(TargetFrameTime - Elapsed);
	}
	EndPlay();
}

void Game::BeginPlay()
{
	// TODO : 일관성 있는 코드 작성 필요. Manager들도 Beginplay를 진행하는 거로 하고, 초기화 및 생성 등은 Beginplay에서 진행. World도 생성하고, Beginplay에서 액터들 생성할 것 
	
	// TODO : ~ Network 코드가 번잡 및 모놀리식 결합성 강함. NetworkManager를 통해 패킷 핸들러 관리, 서버 Connect 및 Session 관리
	// 1. 클라이언트 패킷 핸들러 테이블 초기화
	ServerPacketHandler::Init();

	// 2. Network 초기화
	// ClientService 기동 및 서버에 Connect. 워커 스레드는 생성하지 않고
	// IocpCore::Dispatch(0)을 Tick에서 비차단으로 펌핑한다.
#ifdef _DEBUG
	Network = std::make_shared<ClientService>(NetAddress("127.0.0.1", 9999), []() -> SessionRef { return std::make_shared<GameClientSession>(); });
#else
	Network = std::make_shared<ClientService>(NetAddress("112.151.114.88", 9999), []() -> SessionRef { return std::make_shared<GameClientSession>(); });
#endif
	if (Network->Start() == false)
	{
		::OutputDebugStringA("[Game] ClientService Start failed\n");
		return;
	}

	ClientSession = Network->Connect();
	// TODO : ~ Network 코드 

	// 3. 월드 생성
	World = std::make_unique<UWorld>();

	// 3.4 채팅 패널 — InGame 상태 좌하단에 항상 표시. Login 동안엔 Render 분기에서 자동으로 그려지지 않는다.
	ChatPanel = std::make_unique<UChatPanel>();

	// 3.5 로그인 위젯 생성 — env(D1_LOGIN_ID/PW) 둘 다 설정된 경우에만 숨기고 OnConnected 에서 자동 로그인.
	//     env 없으면 기본 Visible(true) 상태로 사용자 입력 대기.
	LoginWidget = std::make_unique<ULoginWidget>();
	{
		char IdBuf[64] = {};
		char PwBuf[64] = {};
		const DWORD IdLen = ::GetEnvironmentVariableA("D1_LOGIN_ID", IdBuf, sizeof(IdBuf));
		const DWORD PwLen = ::GetEnvironmentVariableA("D1_LOGIN_PW", PwBuf, sizeof(PwBuf));
		const bool bEnvPresent = (IdLen > 0 && IdLen < sizeof(IdBuf)) && (PwLen > 0 && PwLen < sizeof(PwBuf));
		if (bEnvPresent)
			LoginWidget->SetVisible(false);
	}

	// 4. 리소스 로드 시작
	// TODO : 이것도 지금 책임 소재가 Game에 있을게 아니라 ResourceManager의 싱글톤으로 진행되어야 할 것. 다만 World를 소유하고 있지 않으므로 고민해볼 것
	LoadResources();
}

void Game::Tick(float DeltaTime)
{
	// Scene 분기 — Login 일 땐 World 를 Tick 하지 않아 몬스터/플레이어 액터 상태가 멈춘다.
	if (CurrentState == EGameState::InGame)
	{
		// 채팅 엔터 토글: 비활성 → 입력 모드 진입 / 활성 → 메시지 전송 후 모드 종료.
		// World->Tick 보다 먼저 처리해야 같은 프레임의 캐릭터 입력이 채팅 활성 상태에서 차단된다.
		if (ChatPanel != nullptr && InputManager::Get().GetKeyDown(EKey::Enter))
		{
			if (ChatPanel->IsInputActive())
			{
				const std::wstring& InputText = ChatPanel->GetInputText();
				if (InputText.empty() == false && ClientSession != nullptr)
				{
					Protocol::C_CHAT Pkt;
					Pkt.set_text(WideToUtf8(InputText));
					ClientSession->Send(ServerPacketHandler::MakeSendBuffer(Pkt));
				}
				ChatPanel->ClearInputAndEndMode();
			}
			else
			{
				ChatPanel->BeginInput();
			}
		}

		if (ChatPanel != nullptr)
		{
			ChatPanel->Tick(DeltaTime);
		}

		if (World != nullptr)
			World->Tick(DeltaTime);
	}
	else // EGameState::Login
	{
		if (LoginWidget != nullptr)
			LoginWidget->Tick(DeltaTime);
	}

	if (Network == nullptr) return;

	// IOCP 완료는 State 와 무관하게 매 프레임 드레인 — Connect 직후 S_LOGIN 이 이 경로로 도착.
	// Dispatch는 1건 처리마다 반환하므로 루프로 드레인한다.
	while (Network->GetIocpCore()->Dispatch(0))
	{
	}

}

void Game::EndPlay()
{
	World.reset();
	LoginWidget.reset();
	ChatPanel.reset();

	if (Network != nullptr)
	{
		Network->Stop();
	}
	ClientSession.reset();
	Network.reset();
}

void Game::LoadResources()
{
	// TextureEntries 순회 — 새 텍스처는 ResourceManager.cpp 배열에만 추가
	for (int32 i = 0; i < ResourceManager::TextureEntryCount; i++)
	{
		const FTextureEntry& Entry = ResourceManager::TextureEntries[i];
		ResourceManager::Get().Load(Entry.Name, Entry.Path);
	}

	// 모든 Level 의 타일 레이어/충돌 맵을 프리로드 — 포탈 전이 시 디스크 재로드 없이 CurrentLevelID 스위칭만으로 교체된다.
	for (int32 LevelID = 0; LevelID < LEVEL_COUNT; LevelID++)
	{
		// TileLayerEntries 순회 — 레이어 순서대로 해당 LevelID 슬롯에 등록.
		for (int32 i = 0; i < ResourceManager::TileLayerEntryCount; i++)
		{
			const FTileLayerEntry& Entry = ResourceManager::TileLayerEntries[i];
			auto TileMap = std::make_unique<UTileMap>();
			if (TileMap->Load(ResourceManager::Get().ResolveLevelPath(LevelID, Entry.CsvPath), Entry.TilesetName))
			{
				World->AddTileLayer(LevelID, std::move(TileMap));
			}
		}

		// 충돌 맵 로드: 렌더 레이어와 분리된 단일 논리 레이어
		World->SetCollisionMap(LevelID, ResourceManager::Get().LoadCollisionMap(LevelID));
	}
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
	// WS_THICKFRAME, WS_MAXIMIZEBOX 제거 → 크기 조절/최대화 불가
	constexpr DWORD WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT WindowRect = { 0, 0, WindowWidth, WindowHeight };
	::AdjustWindowRect(&WindowRect, WindowStyle, FALSE);
	int32 AdjustedWidth = WindowRect.right - WindowRect.left;
	int32 AdjustedHeight = WindowRect.bottom - WindowRect.top;
	HWnd = ::CreateWindowEx(
		0,
		L"D1GameWindow",
		L"D1 Game Client",
		WindowStyle,
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

void Game::AddDebugLog(const wchar_t* Line)
{
	if (Line == nullptr) return;
	DebugLogs.emplace_back(Line);
	// 오래된 줄부터 폐기해서 최대 N줄만 유지한다.
	while (static_cast<int32>(DebugLogs.size()) > MaxDebugLogLines)
		DebugLogs.pop_front();
}

LRESULT CALLBACK Game::WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case WM_KEYDOWN:
		InputManager::Get().OnKeyDown(static_cast<uint8>(wParam));
		return 0;
	case WM_KEYUP:
		InputManager::Get().OnKeyUp(static_cast<uint8>(wParam));
		return 0;
	case WM_CHAR:
		// WM_CHAR 는 TranslateMessage 가 WM_KEYDOWN 을 변환해서 발행. wParam 은 UTF-16 코드포인트.
		InputManager::Get().OnChar(static_cast<wchar_t>(wParam));
		return 0;
	case WM_LBUTTONDOWN:
		// LOWORD=X, HIWORD=Y, 클라이언트 좌표계.
		InputManager::Get().OnMouseDown(static_cast<int32>(LOWORD(lParam)), static_cast<int32>(HIWORD(lParam)));
		return 0;
	case WM_KILLFOCUS:
		InputManager::Get().ResetAllKeys();
		return 0;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, Msg, wParam, lParam);
}
