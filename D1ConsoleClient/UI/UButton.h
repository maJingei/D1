#pragma once

#include "Core/CoreMinimal.h"
#include "UI/UWidget.h"

#include <functional>
#include <string>

/**
 * 클릭 가능한 금색 버튼.
 * HitTest 는 ULoginWidget 이 Tick 안에서 수행하고, 적중 시 Click() 으로 콜백을 발화한다.
 * 버튼은 마우스 이벤트를 직접 소비하지 않는다 — 상위 위젯이 중재.
 */
class UButton : public UWidget
{
public:
	UButton() = default;
	virtual ~UButton() = default;

	void SetBounds(int32 InX, int32 InY, int32 InW, int32 InH);
	void SetLabel(const wchar_t* InLabel) { Label = (InLabel != nullptr) ? InLabel : L""; }
	void SetOnClick(std::function<void()> InCallback) { OnClick = std::move(InCallback); }

	bool HitTest(int32 MouseX, int32 MouseY) const;
	void Click() const { if (OnClick) OnClick(); }

	virtual void Render(HDC BackDC, int32 AnchorX, int32 AnchorY) override;

private:
	int32 X = 0;
	int32 Y = 0;
	int32 W = 0;
	int32 H = 0;
	std::wstring Label;
	std::function<void()> OnClick;
};