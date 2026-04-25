#include "UI/UChatPanel.h"

#include "Input/InputManager.h"

#include <Windows.h>

// AlphaBlend 는 msimg32.lib 에 정의 — vcxproj 를 건드리지 않고 cpp 단위에서 링크 강제.
#pragma comment(lib, "msimg32.lib")

void UChatPanel::AddLine(const std::wstring& Line)
{
	Lines[LineWriteIndex] = Line;
	LineWriteIndex = (LineWriteIndex + 1) % LineCount;
	if (LineCountUsed < LineCount)
	{
		++LineCountUsed;
	}
}

void UChatPanel::BeginInput()
{
	bInputActive = true;
	InputBuffer.clear();
}

void UChatPanel::CancelInput()
{
	bInputActive = false;
	InputBuffer.clear();
}

void UChatPanel::ClearInputAndEndMode()
{
	InputBuffer.clear();
	bInputActive = false;
}

void UChatPanel::Tick(float /*DeltaTime*/)
{
	if (bInputActive == false)
	{
		return;
	}

	InputManager& Input = InputManager::Get();

	// WM_CHAR 누적 버퍼 소비 — 제어문자(0x20 미만)는 모두 무시. 엔터/Backspace 는 별도 키 입력으로 처리한다.
	for (wchar_t Ch : Input.GetCharBuffer())
	{
		if (Ch < 0x20)
		{
			continue;
		}
		if (static_cast<int32>(InputBuffer.size()) >= MaxInputChars)
		{
			break;
		}
		InputBuffer.push_back(Ch);
	}

	// Backspace 한 글자 제거 — WM_CHAR 의 0x08 은 위에서 걸러졌으므로 키 입력으로 명시 처리.
	if (Input.GetKeyDown(EKey::Backspace) && InputBuffer.empty() == false)
	{
		InputBuffer.pop_back();
	}
}

void UChatPanel::Render(HDC BackDC, int32 AnchorX, int32 AnchorY)
{
	if (IsVisible() == false)
	{
		return;
	}

	// 1) 반투명 검은 배경 — 메모리 DC 에 솔리드 검정을 그린 뒤 AlphaBlend 로 50% 합성.
	//    SourceConstantAlpha=128 (≈50%). GDI 의 FillRect 만으로는 알파 처리가 불가능해 메모리 DC 우회가 필요하다.
	{
		HDC MemDC = ::CreateCompatibleDC(BackDC);
		HBITMAP MemBmp = ::CreateCompatibleBitmap(BackDC, PanelWidth, PanelHeight);
		HGDIOBJ OldBmp = ::SelectObject(MemDC, MemBmp);

		HBRUSH BgBrush = ::CreateSolidBrush(RGB(0, 0, 0));
		RECT MemRect = { 0, 0, PanelWidth, PanelHeight };
		::FillRect(MemDC, &MemRect, BgBrush);
		::DeleteObject(BgBrush);

		BLENDFUNCTION Blend = {};
		Blend.BlendOp = AC_SRC_OVER;
		Blend.SourceConstantAlpha = 128;
		Blend.AlphaFormat = 0;
		::AlphaBlend(BackDC, AnchorX, AnchorY, PanelWidth, PanelHeight, MemDC, 0, 0, PanelWidth, PanelHeight, Blend);

		::SelectObject(MemDC, OldBmp);
		::DeleteObject(MemBmp);
		::DeleteDC(MemDC);
	}

	const int32 OldBkMode = ::SetBkMode(BackDC, TRANSPARENT);
	const COLORREF OldColor = ::SetTextColor(BackDC, RGB(230, 230, 230));
	const UINT OldAlign = ::GetTextAlign(BackDC);
	::SetTextAlign(BackDC, TA_LEFT | TA_TOP);

	// 2) ring 인덱스 산출 — 가장 오래된 줄을 최상단, 최신 줄을 입력 라인 바로 위에 배치한다.
	const int32 TextX = AnchorX + PanelPadding;
	const int32 LinesTopY = AnchorY + PanelPadding;
	const int32 OldestIdx = (LineWriteIndex - LineCountUsed + LineCount) % LineCount;
	for (int32 i = 0; i < LineCountUsed; ++i)
	{
		const int32 RingIdx = (OldestIdx + i) % LineCount;
		const std::wstring& Line = Lines[RingIdx];
		if (Line.empty())
		{
			continue;
		}
		const int32 LineY = LinesTopY + i * LineHeight;
		::TextOutW(BackDC, TextX, LineY, Line.c_str(), static_cast<int32>(Line.size()));
	}

	// 3) 활성 시 입력 라인 — 노란색 prompt + 현재 입력 + 단순 cursor.
	if (bInputActive)
	{
		const int32 InputY = AnchorY + PanelPadding + LineCount * LineHeight;
		::SetTextColor(BackDC, RGB(255, 255, 100));
		std::wstring DisplayInput = L"> " + InputBuffer + L"_";
		::TextOutW(BackDC, TextX, InputY, DisplayInput.c_str(), static_cast<int32>(DisplayInput.size()));
	}

	::SetTextAlign(BackDC, OldAlign);
	::SetTextColor(BackDC, OldColor);
	::SetBkMode(BackDC, OldBkMode);
}