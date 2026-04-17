#pragma once

#include "AEffectActor.h"
#include "GameObject/AActor.h"

/** 공격 피격 시 재생되는 1회성 이펙트. */
class AHitEffectActor : public AEffectActor
{
public:
	/** 피격 위치(월드 픽셀 중앙)에서 이펙트를 스폰한다. */
	AHitEffectActor(float CenterX, float CenterY);

	virtual ~AHitEffectActor() = default;

	/** 중앙 정렬 오프셋을 적용해 렌더. AnimActor 기본 렌더가 좌상단 기준이므로 override 한다. */
	virtual void Render(HDC BackDC) override;

private:
	// 시트 레이아웃 (Hit.bmp 기준)
	static constexpr int32 FrameW = 50;
	static constexpr int32 FrameH = 47;
	static constexpr int32 FrameCount = 6;
	static constexpr float PlaybackFps = 20.f;

	// 렌더 크기: 2배 확대
	static constexpr int32 RenderW = FrameW; // 100
	static constexpr int32 RenderH = FrameH; // 94

	// 컬러 키 (BMP 검정 배경)
	static constexpr uint8 KeyR = 0;
	static constexpr uint8 KeyG = 0;
	static constexpr uint8 KeyB = 0;

	// 클립 ID (단일 클립이지만 Sprite 규약상 필요)
	static constexpr int32 ClipId = 0;
};