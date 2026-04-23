#pragma once

#include "Core/CoreMinimal.h"
#include "UI/UWidget.h"

/**
 * 단일 라인 텍스트 입력 필드.
 * - focus 상태일 때만 InputManager 의 CharBuffer/Backspace 키를 소비한다.
 * - Text 버퍼는 서버 VARCHAR(32) 와 1:1 (32자 + null term), InputManager 에서 이미 ASCII 0x20~0x7E 만 통과.
 * - 파생 UPasswordField 는 BuildDisplayText 를 override 해 '*' 로 대체 렌더.
 */
class UTextField : public UWidget
{
public:
	UTextField() = default;
	virtual ~UTextField() = default;

	/** 위젯 bbox 지정. 좌표는 위젯 렌더 시 AnchorX/AnchorY 와 합산된다. */
	void SetBounds(int32 InX, int32 InY, int32 InW, int32 InH);

	/** focus 토글. ULoginWidget 이 클릭/Tab 라우팅 시 호출한다. */
	void SetFocused(bool bInFocused) { bFocused = bInFocused; }
	bool IsFocused() const { return bFocused; }

	/** 현재 입력된 문자열 (null-term). 송신 시 바로 C_LOGIN.id/pw 에 복사. */
	const char* GetText() const { return Text; }
	int32 GetLength() const { return TextLength; }

	/** 필드 내용 초기화. 실패 재시도 시 PW 필드 전용으로 사용. */
	void Clear();

	/** 클라이언트 좌표가 본 필드 bbox 안인지. ULoginWidget 이 마우스 라우팅 용도로 사용. */
	bool HitTest(int32 MouseX, int32 MouseY) const;

	/** focus 일 때만 CharBuffer + Backspace 를 append/pop 한다. */
	virtual void Tick(float DeltaTime) override;

	virtual void Render(HDC BackDC, int32 AnchorX, int32 AnchorY) override;

protected:
	/** 화면에 실제 표시할 wchar_t 문자열을 OutBuffer 에 채운다. 기본은 Text 를 그대로 wide 확장. */
	virtual void BuildDisplayText(wchar_t* OutBuffer, int32 BufferLen) const;

	/** VARCHAR(32) 와 매치되는 최대 길이. null term 포함 배열 크기는 +1. */
	static constexpr int32 MaxLength = 32;

	int32 X = 0;
	int32 Y = 0;
	int32 W = 0;
	int32 H = 0;
	bool bFocused = false;

	char Text[MaxLength + 1] = {};
	int32 TextLength = 0;
};