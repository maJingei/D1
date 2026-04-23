#include "UI/ULoginWidget.h"

#include "Game.h"
#include "Input/InputManager.h"
#include "Iocp/Session.h"
#include "Network/GameClientSession.h"

ULoginWidget::ULoginWidget()
{
	IdField.SetBounds(FieldX, IdFieldY, FieldW, FieldH);
	PwField.SetBounds(FieldX, PwFieldY, FieldW, FieldH);
	LoginButton.SetBounds(ButtonX, ButtonY, ButtonW, ButtonH);
	LoginButton.SetLabel(L"LOGIN");
	LoginButton.SetOnClick([this]() { SubmitLogin(); });

	// 기본 focus 는 ID 필드 — 사용자가 바로 타이핑 시작 가능.
	IdField.SetFocused(true);
}

void ULoginWidget::SetFocus(UTextField* Target)
{
	IdField.SetFocused(Target == &IdField);
	PwField.SetFocused(Target == &PwField);
}

void ULoginWidget::Tick(float DeltaTime)
{
	if (IsVisible() == false) return;

	InputManager& Input = InputManager::Get();

	// 1) 마우스 클릭 — 필드 focus 전환 or Login 버튼 활성화.
	int32 ClickX = 0;
	int32 ClickY = 0;
	if (Input.ConsumeMouseDown(ClickX, ClickY))
	{
		if (IdField.HitTest(ClickX, ClickY))
		{
			SetFocus(&IdField);
		}
		else if (PwField.HitTest(ClickX, ClickY))
		{
			SetFocus(&PwField);
		}
		else if (LoginButton.HitTest(ClickX, ClickY))
		{
			LoginButton.Click();
		}
	}

	// 2) Tab — ID ↔ PW focus 토글.
	if (Input.GetKeyDown(EKey::Tab))
	{
		if (IdField.IsFocused())
			SetFocus(&PwField);
		else
			SetFocus(&IdField);
	}

	// 3) Enter — 즉시 제출.
	if (Input.GetKeyDown(EKey::Enter))
	{
		SubmitLogin();
	}

	// 4) Esc — focus 된 필드의 Text 만 초기화 (다른 필드는 그대로).
	if (Input.GetKeyDown(EKey::Escape))
	{
		if (IdField.IsFocused())
			IdField.Clear();
		else if (PwField.IsFocused())
			PwField.Clear();
	}

	// 5) 사용자가 이번 프레임 뭔가 타이핑했다면 기존 에러 메시지를 자동으로 걷어낸다.
	if (ErrorMessage.empty() == false && Input.GetCharBuffer().empty() == false)
	{
		ClearError();
	}

	// 6) focused 필드의 Tick — CharBuffer/Backspace 소비.
	IdField.Tick(DeltaTime);
	PwField.Tick(DeltaTime);
}

void ULoginWidget::Render(HDC BackDC, int32 AnchorX, int32 AnchorY)
{
	if (IsVisible() == false) return;

	// 화면 전체 검정 배경 (#0d0b0a) — Level 렌더가 스킵된 상태라 잔상 방지에도 필요.
	RECT Full = { AnchorX, AnchorY, AnchorX + 960, AnchorY + 672 };
	HBRUSH BgBrush = ::CreateSolidBrush(RGB(13, 11, 10));
	::FillRect(BackDC, &Full, BgBrush);
	::DeleteObject(BgBrush);

	// 스톤 패널 (#3a332c)
	RECT Panel = { AnchorX + PanelX, AnchorY + PanelY, AnchorX + PanelX + PanelW, AnchorY + PanelY + PanelH };
	HBRUSH StoneBrush = ::CreateSolidBrush(RGB(58, 51, 44));
	::FillRect(BackDC, &Panel, StoneBrush);
	::DeleteObject(StoneBrush);

	// 패널 상/좌 하이라이트 (#6b5f52)
	HBRUSH HiBrush = ::CreateSolidBrush(RGB(107, 95, 82));
	RECT PanelHi = { Panel.left, Panel.top, Panel.right, Panel.top + 4 };
	::FillRect(BackDC, &PanelHi, HiBrush);
	PanelHi = { Panel.left, Panel.top, Panel.left + 4, Panel.bottom };
	::FillRect(BackDC, &PanelHi, HiBrush);
	::DeleteObject(HiBrush);

	// 패널 하/우 섀도우 (#2a241e)
	HBRUSH LowBrush = ::CreateSolidBrush(RGB(42, 36, 30));
	RECT PanelLow = { Panel.left, Panel.bottom - 4, Panel.right, Panel.bottom };
	::FillRect(BackDC, &PanelLow, LowBrush);
	PanelLow = { Panel.right - 4, Panel.top, Panel.right, Panel.bottom };
	::FillRect(BackDC, &PanelLow, LowBrush);
	::DeleteObject(LowBrush);

	// 타이틀 배너 (#A07A3B)
	RECT Banner = { AnchorX + BannerX, AnchorY + BannerY, AnchorX + BannerX + BannerW, AnchorY + BannerY + BannerH };
	HBRUSH GoldBrush = ::CreateSolidBrush(RGB(160, 122, 59));
	::FillRect(BackDC, &Banner, GoldBrush);
	::DeleteObject(GoldBrush);

	// 배너 상단 하이라이트
	HBRUSH GoldHiBrush = ::CreateSolidBrush(RGB(217, 176, 106));
	RECT BHi = { Banner.left, Banner.top, Banner.right, Banner.top + 4 };
	::FillRect(BackDC, &BHi, GoldHiBrush);
	::DeleteObject(GoldHiBrush);

	// 배너 하단 섀도우
	HBRUSH GoldLowBrush = ::CreateSolidBrush(RGB(61, 42, 16));
	RECT BLow = { Banner.left, Banner.bottom - 4, Banner.right, Banner.bottom };
	::FillRect(BackDC, &BLow, GoldLowBrush);
	::DeleteObject(GoldLowBrush);

	// 배너 라벨 "D1RPG" — 어두운 금(#2a1c08) 중앙 정렬
	::SetBkMode(BackDC, TRANSPARENT);
	::SetTextColor(BackDC, RGB(42, 28, 8));
	HFONT OldFont = static_cast<HFONT>(::SelectObject(BackDC, ::GetStockObject(DEFAULT_GUI_FONT)));
	::DrawTextW(BackDC, L"D1RPG", 5, &Banner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	// ID/Password 라벨 — 회색(#a89b8c)
	::SetTextColor(BackDC, RGB(168, 155, 140));
	::TextOutW(BackDC, AnchorX + FieldX, AnchorY + IdLabelY, L"ID", 2);
	::TextOutW(BackDC, AnchorX + FieldX, AnchorY + PwLabelY, L"Password", 8);

	::SelectObject(BackDC, OldFont);

	// 자식 위젯 렌더.
	IdField.Render(BackDC, AnchorX, AnchorY);
	PwField.Render(BackDC, AnchorX, AnchorY);
	LoginButton.Render(BackDC, AnchorX, AnchorY);

	// 에러 라벨 — 있을 때만 빨강 중앙 정렬.
	if (ErrorMessage.empty() == false)
	{
		::SetBkMode(BackDC, TRANSPARENT);
		::SetTextColor(BackDC, RGB(220, 80, 80));
		HFONT ErrOldFont = static_cast<HFONT>(::SelectObject(BackDC, ::GetStockObject(DEFAULT_GUI_FONT)));
		RECT ErrRect = { AnchorX + ErrorX, AnchorY + ErrorY, AnchorX + ErrorX + ErrorW, AnchorY + ErrorY + ErrorH };
		::DrawTextW(BackDC, ErrorMessage.c_str(), static_cast<int>(ErrorMessage.size()), &ErrRect,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		::SelectObject(BackDC, ErrOldFont);
	}
}

void ULoginWidget::ShowError(const wchar_t* Message)
{
	ErrorMessage = (Message != nullptr) ? Message : L"";
	PwField.Clear();
	SetFocus(&PwField);
	SetVisible(true);
}

void ULoginWidget::ClearError()
{
	ErrorMessage.clear();
}

void ULoginWidget::SubmitLogin()
{
	Game* G = Game::GetInstance();
	if (G == nullptr) return;

	const SessionRef& Sess = G->GetClientSession();
	if (Sess == nullptr) return;

	// 빈 값은 서버까지 가지 않고 즉시 에러 — 서버 검증과 일치시키되 왕복 낭비 방지.
	if (IdField.GetLength() == 0 || PwField.GetLength() == 0)
	{
		ShowError(L"ID/비밀번호를 입력해주세요.");
		return;
	}

	// GameClientSession::SendLoginPacket(id, pw) 오버로드 호출.
	auto GameSess = std::static_pointer_cast<GameClientSession>(Sess);
	const std::string Id(IdField.GetText(), IdField.GetLength());
	const std::string Pw(PwField.GetText(), PwField.GetLength());
	GameSess->SendLoginPacket(Id, Pw);

	ClearError();
}