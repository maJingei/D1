#include "AEffectActor.h"

#include "World/UWorld.h"

AEffectActor::AEffectActor()
{
	// 이펙트는 충돌/이동 계산에서 제외한다. 피격자 위에 겹쳐도 이동을 막지 않는다.
	bBlocksMovement = false;
}

void AEffectActor::Tick(float DeltaTime)
{
	// 1. Sprite 프레임 진행 — AnimActor 가 ActorSprite->Update 호출.
	AnimActor::Tick(DeltaTime);

	// 2. 수명 누적
	ElapsedTime += DeltaTime;

	// 3. 수명 만료 시 자멸. Lifetime<=0 이면 무제한 재생(파생 클래스가 수동 DestroyActor).
	if (Lifetime > 0.f && ElapsedTime >= Lifetime)
	{
		if (World != nullptr)
		{
			// shared_from_this 를 쓰면 std::enable_shared_from_this 가 필요하므로,
			// Actors 목록에서 this 포인터를 찾아 제거하는 방식으로 대체한다.
			const auto& AllActors = World->GetActorsForIteration();
			for (const std::shared_ptr<AActor>& A : AllActors)
			{
				if (A.get() == this)
				{
					World->DestroyActor(A);
					return;
				}
			}
		}
	}
}