#include "Sprite.h"
#include "../Subsystems/Texture.h"

#include <gdiplus.h>

namespace D1
{
	void Sprite::Init(std::shared_ptr<Texture> InTexture, int32 InFrameSize)
	{
		SpriteTexture = InTexture;
		FrameSize = InFrameSize;
		// 기본 출력 크기는 시트 프레임 크기와 동일 (1:1). 확대하려면 SetRenderSize() 호출.
		RenderSize = InFrameSize;
	}

	void Sprite::AddClip(int32 ClipId, FAnimClip Clip)
	{
		Clips[ClipId] = Clip;
	}

	void Sprite::Play(int32 ClipId)
	{
		// 같은 클립이면 무시 — 현재 프레임과 타이머를 유지
		if (CurrentClip == ClipId)
			return;
		CurrentClip  = ClipId;
		CurrentFrame = 0;
		AnimTimer    = 0.f;
	}

	void Sprite::Update(float DeltaTime)
	{
		auto It = Clips.find(CurrentClip);
		if (It == Clips.end())
			return;

		const FAnimClip& Clip = It->second;
		float FrameDuration = 1.f / Clip.Fps;
		AnimTimer += DeltaTime;

		// 누적 시간이 프레임 길이를 초과할 때마다 다음 프레임으로 진행
		while (AnimTimer >= FrameDuration)
		{
			AnimTimer -= FrameDuration;
			CurrentFrame = (CurrentFrame + 1) % Clip.FrameCount;
		}
	}

	void Sprite::Render(HDC BackDC, int32 X, int32 Y, bool bFlipH)
	{
		if (!SpriteTexture)
			return;

		auto It = Clips.find(CurrentClip);
		if (It == Clips.end())
			return;

		Gdiplus::Bitmap* Sheet = SpriteTexture->GetBitmap();
		if (!Sheet)
			return;

		const FAnimClip& Clip = It->second;
		int32 SrcX = CurrentFrame * FrameSize;
		int32 SrcY = Clip.Row * FrameSize;

		Gdiplus::Graphics G(BackDC);
		G.SetPageUnit(Gdiplus::UnitPixel);
		// 픽셀 아트 확대 시 보간 흐려짐 방지 — 1:1일 때도 무해.
		G.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		G.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

		if (!bFlipH)
		{
			Gdiplus::Rect DstRect(X, Y, RenderSize, RenderSize);
			G.DrawImage(Sheet, DstRect, SrcX, SrcY, FrameSize, FrameSize, Gdiplus::UnitPixel);
		}
		else
		{
			// 좌향: 좌표계를 X+RenderSize로 평행이동 후 X축 -1 스케일 → (X..X+RenderSize) 영역에 거울상으로 그려짐.
			G.TranslateTransform(static_cast<Gdiplus::REAL>(X + RenderSize), static_cast<Gdiplus::REAL>(Y));
			G.ScaleTransform(-1.0f, 1.0f);
			Gdiplus::Rect DstRect(0, 0, RenderSize, RenderSize);
			G.DrawImage(Sheet, DstRect, SrcX, SrcY, FrameSize, FrameSize, Gdiplus::UnitPixel);
		}
	}
}