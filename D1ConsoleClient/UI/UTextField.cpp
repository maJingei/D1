#include "UI/UTextField.h"

#include "Input/InputManager.h"

void UTextField::SetBounds(int32 InX, int32 InY, int32 InW, int32 InH)
{
	X = InX; 
	Y = InY;
	W = InW;
	H = InH;
}

void UTextField::Clear()
{
	Text[0] = '\0';
	TextLength = 0;
}

bool UTextField::HitTest(int32 MouseX, int32 MouseY) const
{
	return MouseX >= X && MouseX < X + W && MouseY >= Y && MouseY < Y + H;
}

void UTextField::Tick(float /*DeltaTime*/)
{
	if (bFocused == false) return;

	// Backspace 는 WM_KEYDOWN 경로에서 CurrentKeyStates 에 기록 — GetKeyDown 으로 에지 검출(반복 방지).
	if (InputManager::Get().GetKeyDown(EKey::Backspace) && TextLength > 0)
	{
		--TextLength;
		Text[TextLength] = '\0';
	}

	// WM_CHAR 로 쌓인 이번 프레임 문자들을 append. 32자 도달 시 초과분 drop.
	const std::vector<wchar_t>& Buffer = InputManager::Get().GetCharBuffer();
	for (wchar_t WChar : Buffer)
	{
		if (TextLength >= MaxLength) break;
		// InputManager 가 0x20~0x7E 범위로 이미 필터링 — 안전하게 char 로 축소.
		Text[TextLength++] = static_cast<char>(WChar);
		Text[TextLength] = '\0';
	}
}

void UTextField::BuildDisplayText(wchar_t* OutBuffer, int32 BufferLen) const
{
	int32 i = 0;
	for (; i < TextLength && i < BufferLen - 1; ++i)
		OutBuffer[i] = static_cast<wchar_t>(Text[i]);
	OutBuffer[i] = L'\0';
}

void UTextField::Render(HDC BackDC, int32 AnchorX, int32 AnchorY)
{
	if (IsVisible() == false) return;

	const int32 DrawX = X + AnchorX;
	const int32 DrawY = Y + AnchorY;

	// 입력 박스 배경 (SVG #1a1714)
	RECT BoxRect = { DrawX, DrawY, DrawX + W, DrawY + H };
	HBRUSH BgBrush = ::CreateSolidBrush(RGB(26, 23, 20));
	::FillRect(BackDC, &BoxRect, BgBrush);
	::DeleteObject(BgBrush);

	// 상/좌 dark (#0a0907) — 안쪽 그림자
	HBRUSH DarkBrush = ::CreateSolidBrush(RGB(10, 9, 7));
	RECT Inner = { DrawX, DrawY, DrawX + W, DrawY + 2 };
	::FillRect(BackDC, &Inner, DarkBrush);
	Inner = { DrawX, DrawY, DrawX + 2, DrawY + H };
	::FillRect(BackDC, &Inner, DarkBrush);
	::DeleteObject(DarkBrush);

	// 테두리 (#6b5f52) — focus 일 때 금색(#d9b06a) 2px 로 강조
	const COLORREF BorderColor = bFocused ? RGB(217, 176, 106) : RGB(107, 95, 82);
	HPEN BorderPen = ::CreatePen(PS_SOLID, 2, BorderColor);
	HPEN OldPen = static_cast<HPEN>(::SelectObject(BackDC, BorderPen));
	HBRUSH OldBrush = static_cast<HBRUSH>(::SelectObject(BackDC, ::GetStockObject(NULL_BRUSH)));
	::Rectangle(BackDC, DrawX - 1, DrawY - 1, DrawX + W + 1, DrawY + H + 1);
	::SelectObject(BackDC, OldPen);
	::SelectObject(BackDC, OldBrush);
	::DeleteObject(BorderPen);

	// 텍스트 렌더 — 파생 UPasswordField 가 BuildDisplayText 로 '*' 치환.
	wchar_t Display[MaxLength + 1] = {};
	BuildDisplayText(Display, MaxLength + 1);
	::SetBkMode(BackDC, TRANSPARENT);
	::SetTextColor(BackDC, RGB(220, 200, 170));
	HFONT OldFont = static_cast<HFONT>(::SelectObject(BackDC, ::GetStockObject(DEFAULT_GUI_FONT)));
	::TextOutW(BackDC, DrawX + 10, DrawY + 10, Display, TextLength);
	::SelectObject(BackDC, OldFont);
}