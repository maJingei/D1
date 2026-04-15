#include "UWorld.h"
#include "UTileMap.h"
#include "UCollisionMap.h"

#include "GameObject/AActor.h"

#include <algorithm>

namespace D1
{
	void UWorld::Tick(float DeltaTime)
	{
		// Tick 도중 DestroyActor가 호출되어 Actors가 변해도 안전하도록 snapshot으로 순회한다.
		// shared_ptr 복사는 refcount 증감만 발생하므로 Actor 수백 규모에서는 비용이 무시할 만하다.
		std::vector<std::shared_ptr<AActor>> TickTargets = Actors;
		for (const std::shared_ptr<AActor>& Actor : TickTargets)
		{
			Actor->Tick(DeltaTime);
		}
	}

	void UWorld::Render(HDC BackDC)
	{
		// 1. 배경 타일 레이어를 순서대로 그린다 (Ground → Wall → Water → Actor 순)
		for (const std::unique_ptr<UTileMap>& Layer : TileLayers)
		{
			Layer->Render(BackDC);
		}

		// 2. GameObject 렌더
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

	bool UWorld::CanMoveTo(const AActor* Mover, int32 TileX, int32 TileY) const
	{
		// 방어적: Mover가 없으면 이동 불가로 처리한다.
		if (Mover == nullptr)
			return false;

		// 1. 정적 타일맵 차단 체크 — CollisionMap 미주입이면 타일 차단은 없는 것으로 간주한다.
		if (CollisionMap && CollisionMap->IsBlocked(TileX, TileY))
			return false;

		// 2. 다른 액터 점유 체크 — 자기 자신은 건너뛰고, bBlocksMovement가 false인 액터는 무시한다.
		// TODO : Actors 순회 가드 필요(복사를 한다거나). 만약 Actors를 순회하다가 하나의 Actor가 삭제된다면 ? 
		for (const std::shared_ptr<AActor>& Other : Actors)
		{
			if (Other.get() == Mover)
				continue;
			if (!Other->bBlocksMovement)
				continue;
			if (Other->GetTileX() == TileX && Other->GetTileY() == TileY)
				return false;
		}
		return true;
	}
}