#pragma once

#include "UI/UWidget.h"

#include <memory>

class Texture;

/** HP 비율을 두 프레임 합성으로 렌더하는 체력바 위젯. */
class UHealthBarWidget : public UWidget
{
public:
	UHealthBarWidget();
	virtual ~UHealthBarWidget() = default;

	virtual void Render(HDC BackDC, int32 AnchorX, int32 AnchorY) override;

	/** 현재 HP 와 최대 HP 를 갱신한다. */
	void SetHP(int32 InCurrent, int32 InMax);

private:
	/** HealthBar-Sheet.png 텍스처. 생성 시 ResourceManager 에서 조회. */
	std::shared_ptr<Texture> SheetTexture;

	// ---------------------------------------------------------------
	// 시트 소스 레이아웃 (HealthBar-Sheet.png 기준)
	// ---------------------------------------------------------------

	static constexpr int32 SrcX          = 0;
	static constexpr int32 BarSrcW       = 124;
	static constexpr int32 BarSrcH       = 15;
	static constexpr int32 EmptyBarSrcY  = 57;
	static constexpr int32 FilledBarSrcY = 98;

	// ---------------------------------------------------------------
	// 렌더 출력 크기 및 앵커 오프셋
	// ---------------------------------------------------------------

	/** 화면상 체력바 가로 픽셀. 원본 124 에서 축소 출력. */
	static constexpr int32 RenderW = 70;

	/** 화면상 체력바 세로 픽셀. */
	static constexpr int32 RenderH = 8;

	/** AnchorX 기준 좌측으로 RenderW/2 만큼 이동해 중앙 정렬. */
	static constexpr int32 OffsetX = (-RenderW / 2);

	/** AnchorY 기준 위쪽으로 띄울 픽셀 — 머리 위 배치. */
	static constexpr int32 OffsetY = 0;

	// ---------------------------------------------------------------
	// 상태
	// ---------------------------------------------------------------

	int32 CurrentHP = 0;
	int32 MaxHP = 1;
};