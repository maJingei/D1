#pragma once

#include "GameObject/AnimActor.h"

/** 일회성 시각 효과 Actor 의 공용 베이스. */
class AEffectActor : public AnimActor
{
public:
	AEffectActor();
	virtual ~AEffectActor() = default;

	/** 수명 타이머 진행 + Sprite 프레임 업데이트. 수명 만료 시 World 에서 자기 자신을 DestroyActor. */
	virtual void Tick(float DeltaTime) override;

	/** 총 수명(초). 0 이하이면 무제한(수동 소멸). */
	void SetLifetime(float InLifetime) { Lifetime = InLifetime; }

	/** 현재까지 누적된 재생 시간. */
	float GetElapsedTime() const { return ElapsedTime; }

protected:
	/** 총 수명(초). 파생 클래스가 생성자에서 세팅한다. */
	float Lifetime = 0.f;

	/** 생성 이후 누적 시간(초). Tick 에서 증가. */
	float ElapsedTime = 0.f;
};