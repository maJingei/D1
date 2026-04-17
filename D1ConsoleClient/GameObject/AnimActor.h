#pragma once

#include "AActor.h"
#include "Render/Sprite.h"

#include <memory>

/** Sprite 기반 렌더링을 공용화한 액터 베이스. */
class AnimActor : public AActor
{
public:
	AnimActor() = default;
	virtual ~AnimActor() = default;

	/** Sprite 프레임 애니메이션을 진행한다. 파생 클래스가 override할 경우 반드시 Super::Tick 호출. */
	virtual void Tick(float DeltaTime) override;

	/** Sprite를 현재 X/Y 픽셀 좌표에 기본 정렬로 렌더링한다. 세밀한 오프셋은 파생 클래스가 override. */
	virtual void Render(HDC BackDC) override;

	/** ActorSprite 접근자 (파생 클래스에서 클립 전환/초기화용). */
	const std::shared_ptr<Sprite>& GetSprite() const { return ActorSprite; }

protected:
	/** 파생 클래스가 소유/초기화하는 스프라이트. nullptr 이면 Tick/Render는 no-op. */
	std::shared_ptr<Sprite> ActorSprite;
};
