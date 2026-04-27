#include "UWorld.h"
#include "UTileMap.h"
#include "UCollisionMap.h"
#include "Render/Texture.h"

#include "GameObject/AActor.h"

#include <gdiplus.h>

#include <algorithm>

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
	// 1. 배경 렌더 — 모든 Level 이 단일 이미지 모드. PNG 한 장을 (0,0) 에 통째로 그린다.
	//    (마일스톤 1 에서 사용하던 LevelUseSingleImage 분기와 3-레이어 TileLayers 경로는 제거됨.)
	const std::shared_ptr<Texture>& BackgroundImage = PerLevel[CurrentLevelID].BackgroundImage;
	if (BackgroundImage != nullptr)
	{
		Gdiplus::Bitmap* BackgroundBitmap = BackgroundImage->GetBitmap();
		if (BackgroundBitmap != nullptr)
		{
			Gdiplus::Graphics G(BackDC);
			G.SetPageUnit(Gdiplus::UnitPixel);
			G.DrawImage(BackgroundBitmap, 0, 0, static_cast<INT>(BackgroundBitmap->GetWidth()), static_cast<INT>(BackgroundBitmap->GetHeight()));
		}
	}

	// 2. GameObject 렌더
	for (const std::shared_ptr<AActor>& Actor : Actors)
	{
		Actor->Render(BackDC);
	}
}

void UWorld::AddTileLayer(int32 LevelID, std::unique_ptr<UTileMap> InTileMap)
{
	if (LevelID < 0 || LevelID >= LEVEL_COUNT)
		return;
	PerLevel[LevelID].TileLayers.push_back(std::move(InTileMap));
}

void UWorld::SetCollisionMap(int32 LevelID, std::shared_ptr<UCollisionMap> InCollisionMap)
{
	if (LevelID < 0 || LevelID >= LEVEL_COUNT)
		return;
	PerLevel[LevelID].CollisionMap = std::move(InCollisionMap);
}

void UWorld::SetBackgroundImage(int32 LevelID, std::shared_ptr<Texture> InBackgroundImage)
{
	if (LevelID < 0 || LevelID >= LEVEL_COUNT)
	{
		return;
	}
	PerLevel[LevelID].BackgroundImage = std::move(InBackgroundImage);
}

void UWorld::SetCurrentLevelID(int32 InLevelID)
{
	// 범위 밖이면 현재 Level 을 유지해 렌더/충돌이 무효 Level 을 참조하지 않도록 한다.
	if (InLevelID < 0 || InLevelID >= LEVEL_COUNT)
		return;
	CurrentLevelID = InLevelID;
}

void UWorld::DestroyActor(const std::shared_ptr<AActor>& Actor)
{
	auto It = std::find(Actors.begin(), Actors.end(), Actor);
	if (It != Actors.end())
	{
		Actors.erase(It);
	}
}

std::shared_ptr<APlayerActor> UWorld::FindPlayerActor(uint64 PlayerID)
{
	auto It = PlayerMap.find(PlayerID);
	if (It == PlayerMap.end())
		return nullptr;

	// weak_ptr 이 이미 expire 됐다면 맵에서 즉시 제거해 누적되지 않도록 한다.
	std::shared_ptr<APlayerActor> Locked = It->second.lock();
	if (Locked == nullptr)
		PlayerMap.erase(It);
	return Locked;
}

std::shared_ptr<AMonsterActor> UWorld::FindMonsterActor(uint64 MonsterID)
{
	auto It = MonsterMap.find(MonsterID);
	if (It == MonsterMap.end())
		return nullptr;

	std::shared_ptr<AMonsterActor> Locked = It->second.lock();
	if (Locked == nullptr)
		MonsterMap.erase(It);
	return Locked;
}

bool UWorld::CanMoveTo(const AActor* Mover, int32 TileX, int32 TileY) const
{
	// 방어적: Mover가 없으면 이동 불가로 처리한다.
	if (Mover == nullptr)
		return false;

	// 1. 정적 타일맵 차단 체크 — 현재 Level CollisionMap 미주입이면 타일 차단은 없는 것으로 간주한다.
	const std::shared_ptr<UCollisionMap>& CurrentCollision = PerLevel[CurrentLevelID].CollisionMap;
	if (CurrentCollision && CurrentCollision->IsBlocked(TileX, TileY))
		return false;

	// 2. 다른 액터 점유 체크 — 자기 자신은 건너뛰고, bBlocksMovement가 false인 액터는 무시한다.
	const std::vector<std::shared_ptr<AActor>> Snapshot = Actors;
	for (const std::shared_ptr<AActor>& Other : Snapshot)
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
