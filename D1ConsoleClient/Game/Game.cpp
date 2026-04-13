#include "Game.h"

#include "World/UWorld.h"
#include "World/UTileMap.h"
#include "Actor/APlayerActor.h"

#include "../Subsystems/TimeManager.h"
#include "../Subsystems/InputManager.h"
#include "../Subsystems/Renderer.h"
#include "../Subsystems/ResourceManager.h"

#include "../ClientService.h"
#include "../ServerPacketHandler.h"
#include "Iocp/Session.h"
#include "Iocp/PacketSession.h"
#include "Iocp/PacketHeader.h"
#include "Iocp/NetAddress.h"
#include "Iocp/SendBuffer.h"
#include "Iocp/IocpCore.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace D1
{
	namespace
	{
		/**
		 * D1ConsoleClient 전용 Session 파생.
		 * PacketSession 을 상속하여 PacketHeader 기반 프레이밍을 사용한다.
		 *
		 * - OnConnected : 접속 완료 즉시 C_LOGIN 패킷을 자동 송신한다.
		 * - OnRecvPacket: 수신한 완전한 패킷 1건을 ServerPacketHandler 테이블로 디스패치한다.
		 */
		class GameClientSession : public PacketSession
		{
		protected:
			void OnConnected() override
			{
				// 베이스가 RegisterRecv 를 돌려주므로 먼저 수신 대기를 시작시킨 뒤,
				// 바로 LOGIN 요청 패킷을 올려 보낸다.
				Session::OnConnected();
				SendLoginPacket();
			}

			void OnRecvPacket(BYTE* Buffer, int32 Len) override
			{
				PacketSessionRef Ref = GetPacketSessionRef();
				ServerPacketHandler::HandlePacket(Ref, Buffer, Len);
			}

			void OnSend(int32 /*NumOfBytes*/) override
			{
				// 클라이언트에서는 송신 완료 로그를 생략한다.
			}

		private:
			/** 고정 자격증명으로 C_LOGIN 패킷을 인코딩하여 전송한다. */
			void SendLoginPacket()
			{
				Protocol::C_LOGIN pkt;
				pkt.set_id("testuser");
				pkt.set_pw("1234");
				Send(ServerPacketHandler::MakeSendBuffer(pkt));
			}
		};

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

	Game& Game::Get()
	{
		static Game* Instance = new Game();
		return *Instance;
	}

	// UWorld의 완전한 타입이 Game.cpp에서 포함된 후 소멸자를 정의한다.
	Game::~Game() = default;

	bool Game::Initialize(HINSTANCE hInstance)
	{
		HInstance = hInstance;
		if (!CreateGameWindow())
		{
			return false;
		}
		// 서브시스템 초기화 (순서 중요: TimeManager → InputManager → Renderer → ResourceManager)
		TimeManager::Get().Initialize();
		InputManager::Get().Initialize();
		if (!Renderer::Get().Initialize(HWnd, WindowWidth, WindowHeight))
		{
			return false;
		}
		ResourceManager::Get().Initialize();
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
			if (InputManager::Get().GetKeyDown(EKey::Escape))
			{
				bIsRunning = false;
				break;
			}
			
			Tick(TimeManager::Get().GetDeltaTime());
			
			Renderer::Get().BeginRender();
			if (World != nullptr)
			{
				World->Render(Renderer::Get().GetBackBufferDC());
			}
			Renderer::Get().EndRender();
			
			UpdateWindowTitle();
		}
		EndPlay();
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
	}

	void Game::BeginPlay()
	{
		// 클라이언트 패킷 핸들러 테이블 초기화 (Session 생성 이전이어야 안전)
		ServerPacketHandler::Init();

		// ClientService 기동 및 서버에 Connect. 워커 스레드는 생성하지 않고
		// IocpCore::Dispatch(0)을 Tick에서 비차단으로 펌핑한다.
		Network = std::make_shared<ClientService>();
		Network->SetSessionFactory([]() -> SessionRef
		{
			return std::make_shared<GameClientSession>();
		});
		if (Network->Start() == false)
		{
			::OutputDebugStringA("[Game] ClientService Start failed\n");
			return;
		}

		NetAddress ServerAddr("127.0.0.1", 9999);
		ClientSession = Network->Connect(ServerAddr);

		// 월드 생성
		World = std::make_unique<UWorld>();
		LoadResources();

		// 플레이어 액터 등록
		World->SpawnActor<APlayerActor>();
	}

	void Game::Tick(float DeltaTime)
	{
		if (World != nullptr)
		{
			World->Tick(DeltaTime);
		}

		if (Network == nullptr) return;

		// 이번 프레임에 도착한 IOCP 완료를 모두 소비한다 (비차단).
		// Dispatch는 1건 처리마다 반환하므로 루프로 드레인한다.
		while (Network->GetIocpCore()->Dispatch(0))
		{
		}

		// Enter 키로 추가 C_LOGIN 재전송 (프레이밍 연속 수신 확인용).
		if (ClientSession != nullptr && InputManager::Get().GetKeyDown(EKey::Enter))
		{
			Protocol::C_LOGIN pkt;
			pkt.set_id("testuser");
			pkt.set_pw("1234");
			ClientSession->Send(ServerPacketHandler::MakeSendBuffer(pkt));
		}
	}

	void Game::EndPlay()
	{
		World.reset();

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

		// TileLayerEntries 순회 — 레이어 순서대로 월드에 등록
		for (int32 i = 0; i < ResourceManager::TileLayerEntryCount; i++)
		{
			const FTileLayerEntry& Entry = ResourceManager::TileLayerEntries[i];
			auto TileMap = std::make_unique<UTileMap>();
			if (TileMap->Load(Entry.CsvPath, Entry.TilesetName))
			{
				World->AddTileLayer(std::move(TileMap));
			}
		}
	}

	void Game::OnEchoReceived(const uint8* Data, int32 NumOfBytes)
	{
		std::lock_guard<std::mutex> Lock(EchoMutex);
		LastEcho.assign(reinterpret_cast<const char*>(Data), static_cast<size_t>(NumOfBytes));
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

	void Game::UpdateWindowTitle()
	{
		float FPS = TimeManager::Get().GetFPS();
		float DT = TimeManager::Get().GetDeltaTime();

		// 마지막 Echo 문자열을 와이드로 변환 (ASCII 가정, 표시용)
		std::string EchoSnapshot;
		{
			std::lock_guard<std::mutex> Lock(EchoMutex);
			EchoSnapshot = LastEcho;
		}
		wchar_t EchoWide[128] = {};
		if (EchoSnapshot.empty() == false)
		{
			// ASCII → wide 단순 변환 (표시 목적이므로 MultiByteToWideChar까지는 불필요)
			size_t Max = EchoSnapshot.size() < 127 ? EchoSnapshot.size() : 127;
			for (size_t i = 0; i < Max; ++i)
			{
				EchoWide[i] = static_cast<wchar_t>(static_cast<unsigned char>(EchoSnapshot[i]));
			}
		}

		wchar_t Title[256] = {};
		::swprintf_s(Title, L"D1 Game Client | FPS: %.1f | DT: %.4f | Echo: %s",
			FPS, DT, EchoWide[0] ? EchoWide : L"(none)");
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
