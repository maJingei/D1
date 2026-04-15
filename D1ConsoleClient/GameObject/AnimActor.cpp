#include "AnimActor.h"

namespace D1
{
	void AnimActor::Tick(float DeltaTime)
	{
		if (ActorSprite)
			ActorSprite->Update(DeltaTime);
	}

	void AnimActor::Render(HDC BackDC)
	{
		if (ActorSprite)
			ActorSprite->Render(BackDC, static_cast<int32>(X), static_cast<int32>(Y), false);
	}
}