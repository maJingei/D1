#include "UHealthBarWidget.h"

#include "Render/ResourceManager.h"
#include "Render/Texture.h"

#include <gdiplus.h>

UHealthBarWidget::UHealthBarWidget()
{
	// 텍스처는 이미 ResourceManager::TextureEntries 에 등록되어 로드돼 있다고 가정한다.
	SheetTexture = ResourceManager::Get().GetTexture(L"HealthBarSheet");
}

void UHealthBarWidget::SetHP(int32 InCurrent, int32 InMax)
{
	CurrentHP = InCurrent;
	MaxHP = (InMax > 0) ? InMax : 1;
}

void UHealthBarWidget::Render(HDC BackDC, int32 AnchorX, int32 AnchorY)
{
	if (!bVisible) return;
	if (!SheetTexture) return;
	Gdiplus::Bitmap* Sheet = SheetTexture->GetBitmap();
	if (!Sheet) return;

	const int32 DstX = AnchorX + OffsetX;
	const int32 DstY = AnchorY + OffsetY;

	Gdiplus::Graphics G(BackDC);
	G.SetPageUnit(Gdiplus::UnitPixel);
	G.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	G.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

	// 1) 배경(빈 바) — 전체 너비.
	Gdiplus::Rect BgDst(DstX, DstY, RenderW, RenderH);
	G.DrawImage(Sheet, BgDst, SrcX, EmptyBarSrcY, BarSrcW, BarSrcH, Gdiplus::UnitPixel);

	// 2) 전경(채운 바) — HP / MaxHP 비율로 가로 clip.
	float Ratio = static_cast<float>(CurrentHP) / static_cast<float>(MaxHP);
	if (Ratio < 0.f) Ratio = 0.f;
	if (Ratio > 1.f) Ratio = 1.f;

	const int32 FilledDstW = static_cast<int32>(static_cast<float>(RenderW) * Ratio);
	const int32 FilledSrcW = static_cast<int32>(static_cast<float>(BarSrcW) * Ratio);
	if (FilledDstW <= 0 || FilledSrcW <= 0)
		return;

	Gdiplus::Rect FillDst(DstX, DstY, FilledDstW, RenderH);
	G.DrawImage(Sheet, FillDst, SrcX, FilledBarSrcY, FilledSrcW, BarSrcH, Gdiplus::UnitPixel);
}