#pragma once

#include "Core/CoreMinimal.h"
#include "UI/UWidget.h"

#include <array>
#include <string>

/**
 * 좌하단 Room 채팅 패널. 8 라인 ring buffer + 활성 시 입력 라인.
 *
 * 입력 흐름:
 * 1) Game::Tick 가 InGame 상태에서 엔터 키로 BeginInput / SubmitInput-via-Game / CancelInput 을 호출한다.
 * 2) 활성 동안 Tick 이 InputManager::GetCharBuffer 를 누적해 InputBuffer 에 append.
 * 3) Submit 시 Game 이 GetInputText 로 가져가 C_CHAT 송신 후 ClearInputAndEndMode 호출.
 *
 * UTextField 를 쓰지 않은 이유: UTextField 는 ASCII 0x20~0x7E 만 통과시키도록 설계되어 한글이 들어가지 않는다.
 * 채팅은 한글을 받을 수 있어야 하므로 자체 wchar_t 버퍼를 둔다.
 */
class UChatPanel : public UWidget
{
public:
	UChatPanel() = default;
	virtual ~UChatPanel() = default;

	/** 메시지 한 줄을 ring 에 push. 가장 오래된 줄이 자연스럽게 밀려난다. */
	void AddLine(const std::wstring& Line);

	/** 입력 모드 진입 — 입력 버퍼 초기화 후 활성화. 엔터 토글 ON 경로. */
	void BeginInput();

	/** 입력 모드 취소 — 버퍼 폐기 + 비활성. ESC 또는 빈 메시지 엔터 케이스. */
	void CancelInput();

	/** 송신 후 호출 — 버퍼 폐기 + 비활성. */
	void ClearInputAndEndMode();

	bool IsInputActive() const { return bInputActive; }
	const std::wstring& GetInputText() const { return InputBuffer; }

	virtual void Tick(float DeltaTime) override;
	virtual void Render(HDC BackDC, int32 AnchorX, int32 AnchorY) override;

	// ---------------------------------------------------------------
	// 사양 상수 — Spec 의 "8라인 고정 / 256자 cap" 결정에서 도출.
	// ---------------------------------------------------------------

	static constexpr int32 LineCount         = 8;
	static constexpr int32 LineHeight        = 18;
	static constexpr int32 InputLineHeight   = 20;
	static constexpr int32 PanelWidth        = 480;
	static constexpr int32 PanelPadding      = 6;
	static constexpr int32 PanelHeight       = LineCount * LineHeight + InputLineHeight + PanelPadding * 2;

	/** wchar_t 단위 입력 글자 수 cap. UTF-8 byte 로 환산해도 서버의 256-byte cap 안쪽에 머문다. */
	static constexpr int32 MaxInputChars     = 128;

private:
	std::array<std::wstring, LineCount> Lines;
	int32 LineWriteIndex = 0;
	int32 LineCountUsed  = 0;

	std::wstring InputBuffer;
	bool bInputActive = false;
};