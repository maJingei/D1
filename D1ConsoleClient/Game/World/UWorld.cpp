#include "UWorld.h"
#include "UTileMap.h"

#include "../Actor/AActor.h"

#include <algorithm>

namespace D1
{
	void UWorld::Tick(float DeltaTime)
	{
		for (const std::shared_ptr<AActor>& Actor : Actors)
		{
			Actor->Tick(DeltaTime);
		}
	}

	void UWorld::Render(HDC BackDC)
	{
		// 배경 타일 레이어를 순서대로 그린다 (Ground → Wall → Water → Actor 순)
		for (const std::unique_ptr<UTileMap>& Layer : TileLayers)
		{
			Layer->Render(BackDC);
		}

		for (const std::shared_ptr<AActor>& Actor : Actors)
		{
			Actor->Render(BackDC);
		}
	}

	void UWorld::AddTileLayer(std::unique_ptr<UTileMap> InTileMap)
	{
		TileLayers.push_back(std::move(InTileMap));
	}

	void UWorld::DestroyActor(const std::shared_ptr<AActor>& Actor)
	{
		auto It = std::find(Actors.begin(), Actors.end(), Actor);
		if (It != Actors.end())
		{
			Actors.erase(It);
		}
	}
}