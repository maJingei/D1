#include "UI/UButton.h"

void UButton::SetBounds(int32 InX, int32 InY, int32 InW, int32 InH)
{
	X = InX;
	Y = InY;
	W = InW;
	H = InH;
}

bool UButton::HitTest(int32 MouseX, int32 MouseY) const
{
	return MouseX >= X && MouseX < X + W && MouseY >= Y && MouseY < Y + H;
}

void UButton::Render(HDC BackDC, int32 AnchorX, int32 AnchorY)
{
	if (IsVisible() == false) return;

	const int32 DrawX = X + AnchorX;
	const int32 DrawY = Y + AnchorY;

	// 버튼 본체 — SVG #A07A3B 금색
	RECT Button = { DrawX, DrawY, DrawX + W, DrawY + H };
	HBRUSH GoldBrush = ::CreateSolidBrush(RGB(160, 122, 59));
	::FillRect(BackDC, &Button, GoldBrush);
	::DeleteObject(GoldBrush);

	// 상/좌 하이라이트 2px — SVG #d9b06a
	HBRUSH HiBrush = ::CreateSolidBrush(RGB(217, 176, 106));
	RECT Hi = { DrawX, DrawY, DrawX + W, DrawY + 2 };
	::FillRect(BackDC, &Hi, HiBrush);
	Hi = { DrawX, DrawY, DrawX + 2, DrawY + H };
	::FillRect(BackDC, &Hi, HiBrush);
	::DeleteObject(HiBrush);

	// 하/우 섀도우 2px — SVG #3d2a10
	HBRUSH LowBrush = ::CreateSolidBrush(RGB(61, 42, 16));
	RECT Low = { DrawX, DrawY + H - 2, DrawX + W, DrawY + H };
	::FillRect(BackDC, &Low, LowBrush);
	Low = { DrawX + W - 2, DrawY, DrawX + W, DrawY + H };
	::FillRect(BackDC, &Low, LowBrush);
	::DeleteObject(LowBrush);

	// 라벨 — 어두운 금(#2a1c08) 중앙 정렬
	::SetBkMode(BackDC, TRANSPARENT);
	::SetTextColor(BackDC, RGB(42, 28, 8));
	HFONT OldFont = static_cast<HFONT>(::SelectObject(BackDC, ::GetStockObject(DEFAULT_GUI_FONT)));
	::DrawTextW(BackDC, Label.c_str(), static_cast<int>(Label.size()), &Button,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	::SelectObject(BackDC, OldFont);
}