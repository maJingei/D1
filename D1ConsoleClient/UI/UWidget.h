#pragma once

#include "Core/CoreMinimal.h"

/** UI 요소의 공용 베이스. */
class UWidget
{
public:
	UWidget() = default;
	virtual ~UWidget() = default;

	/** 위젯 상태 갱신 (애니메이션 페이드, 깜빡임 등). 기본 구현은 no-op. */
	virtual void Tick(float DeltaTime) {}

	/** 위젯을 BackDC 에 렌더한다. */
	virtual void Render(HDC BackDC, int32 AnchorX, int32 AnchorY) {}

	void SetVisible(bool bInVisible) { bVisible = bInVisible; }
	bool IsVisible() const { return bVisible; }

protected:
	/** 렌더 가시성 플래그. false 면 파생 Render 가 조기 return 한다(파생 측 약속). */
	bool bVisible = true;
};