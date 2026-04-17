#include "Sprite.h"
#include "Texture.h"

#include <gdiplus.h>

void Sprite::Init(std::shared_ptr<Texture> InTexture, int32 InFrameSize)
{
	// 정사각형 프레임: W/H 를 동일 값으로 위임한다.
	Init(InTexture, InFrameSize, InFrameSize);
}

void Sprite::Init(std::shared_ptr<Texture> InTexture, int32 InFrameW, int32 InFrameH)
{
	SpriteTexture = InTexture;
	FrameW = InFrameW;
	FrameH = InFrameH;
	// 기본 출력 크기는 시트 프레임 크기와 동일 (1:1). 확대하려면 SetRenderSize() 호출.
	RenderW = InFrameW;
	RenderH = InFrameH;
}

void Sprite::SetColorKey(uint8 R, uint8 G, uint8 B)
{
	bUseColorKey = true;
	ColorKeyR = R;
	ColorKeyG = G;
	ColorKeyB = B;
}

void Sprite::AddClip(int32 ClipId, FAnimClip Clip)
{
	Clips[ClipId] = Clip;
}

void Sprite::SetClipId(int32 ClipId)
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

bool Sprite::IsOnLastFrame() const
{
	auto It = Clips.find(CurrentClip);
	if (It == Clips.end())
		return false;
	return CurrentFrame == It->second.FrameCount - 1;
}

int32 Sprite::GetCurrentClipFrameCount() const
{
	auto It = Clips.find(CurrentClip);
	if (It == Clips.end())
		return 0;
	return It->second.FrameCount;
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
	int32 SrcX = CurrentFrame * FrameW;
	int32 SrcY = Clip.Row * FrameH;

	Gdiplus::Graphics G(BackDC);
	G.SetPageUnit(Gdiplus::UnitPixel);
	// 픽셀 아트 확대 시 보간 흐려짐 방지 — 1:1일 때도 무해.
	G.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	G.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

	// 컬러 키 활성 시 ImageAttributes 구성 (배경색 투명 처리).
	Gdiplus::ImageAttributes Attr;
	Gdiplus::ImageAttributes* pAttr = nullptr;
	if (bUseColorKey)
	{
		Gdiplus::Color Key(ColorKeyR, ColorKeyG, ColorKeyB);
		Attr.SetColorKey(Key, Key, Gdiplus::ColorAdjustTypeDefault);
		pAttr = &Attr;
	}

	if (!bFlipH)
	{
		Gdiplus::Rect DstRect(X, Y, RenderW, RenderH);
		G.DrawImage(Sheet, DstRect, SrcX, SrcY, FrameW, FrameH, Gdiplus::UnitPixel, pAttr);
	}
	else
	{
		// 좌향: 좌표계를 X+RenderW로 평행이동 후 X축 -1 스케일 → (X..X+RenderW) 영역에 거울상으로 그려짐.
		G.TranslateTransform(static_cast<Gdiplus::REAL>(X + RenderW), static_cast<Gdiplus::REAL>(Y));
		G.ScaleTransform(-1.0f, 1.0f);
		Gdiplus::Rect DstRect(0, 0, RenderW, RenderH);
		G.DrawImage(Sheet, DstRect, SrcX, SrcY, FrameW, FrameH, Gdiplus::UnitPixel, pAttr);
	}
}