#pragma once

#include "AActor.h"
#include "Render/Sprite.h"

#include <memory>

namespace D1
{
	/**
	 * Sprite 기반 렌더링을 공용화한 액터 베이스.
	 * AActor의 최소 인터페이스 위에 "Sprite 소유 / Update / Render" 만을 추가한다.
	 * 이동·입력·상태머신 등 파생 클래스별 책임은 여기에 두지 않는다 (최소 범위 원칙).
	 *
	 * 사용 방법:
	 *  - 파생 클래스 생성자에서 ActorSprite = std::make_shared<Sprite>() 후 Init/AddClip/SetClipId.
	 *  - 파생 클래스 Tick에서 AnimActor::Tick(DeltaTime) 호출 (또는 본 클래스 Tick을 호출).
	 *  - 파생 클래스 Render에서 ActorSprite를 직접 Render하거나 AnimActor::Render를 override.
	 */
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
}