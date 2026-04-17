#include "AHitEffectActor.h"

#include "Render/ResourceManager.h"
#include "Render/Sprite.h"

AHitEffectActor::AHitEffectActor(float CenterX, float CenterY)
{
	// 중심 좌표를 좌상단 기준으로 변환해 저장한다 (Render 에서 한 번 더 빼지 않기 위함).
	// X/Y 는 AActor 의 픽셀 좌상단 앵커. 확대 출력 크기(RenderW/RenderH) 절반만큼 좌상으로 당긴다.
	X = CenterX - static_cast<float>(RenderW) / 2.f;
	Y = CenterY - static_cast<float>(RenderH) / 2.f;

	// 수명: 6 프레임 / 20 FPS = 0.3초
	SetLifetime(static_cast<float>(FrameCount) / PlaybackFps);

	// Sprite 초기화 — Hit.bmp 는 50×47 직사각형 프레임.
	ActorSprite = std::make_shared<Sprite>();
	auto HitTexture = ResourceManager::Get().GetTexture(L"HitEffect");
	ActorSprite->Init(HitTexture, FrameW, FrameH);
	ActorSprite->SetRenderSize(RenderW, RenderH);
	ActorSprite->SetColorKey(KeyR, KeyG, KeyB);

	// 단일 클립: row 0, 6 프레임, 20 FPS.
	ActorSprite->AddClip(ClipId, { 0, FrameCount, PlaybackFps });
	ActorSprite->SetClipId(ClipId);
}

void AHitEffectActor::Render(HDC BackDC)
{
	// X/Y 는 이미 RenderW/RenderH 를 고려해 좌상단으로 보정돼 저장됨 → 그대로 넘긴다.
	if (ActorSprite)
		ActorSprite->Render(BackDC, static_cast<int32>(X), static_cast<int32>(Y), false);
}