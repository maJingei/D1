#pragma once

#include "Core/CoreMinimal.h"
#include "GameObject/AActor.h"

class Level;

/**
 * Level 이 생성할 Portal 한 개의 구성 값. World 가 LevelID 마다 서로 다른 값을 전달한다.
 * TileX/Y 는 Portal 이 위치할 타일, Target* 은 접촉 시 전이될 Level 인덱스와 도착 타일.
 */
struct FPortalConfig
{
	int32 TileX = 0;
	int32 TileY = 0;
	int32 TargetLevelID = -1;
	int32 TargetSpawnTileX = 0;
	int32 TargetSpawnTileY = 0;
};

/**
 * 타일 기반 포탈 액터. 플레이어가 이 액터가 점유한 타일에 도달하면
 * 소속 Level 이 Target 정보를 참조하여 다른 Level 로 전이시킨다.
 */
class APortalActor : public AActor
{
public:
	APortalActor(uint64 InPortalID, const FPortalConfig& InConfig, std::shared_ptr<Level> InLevel)
		: AActor(InConfig.TileX, InConfig.TileY)
		, ParentLevel(InLevel)
		, PortalID(InPortalID)
		, TargetLevelID(InConfig.TargetLevelID)
		, TargetSpawnTileX(InConfig.TargetSpawnTileX)
		, TargetSpawnTileY(InConfig.TargetSpawnTileY)
	{}

	uint64 GetPortalID() const { return PortalID; }
	int32 GetTargetLevelID() const { return TargetLevelID; }
	int32 GetTargetSpawnTileX() const { return TargetSpawnTileX; }
	int32 GetTargetSpawnTileY() const { return TargetSpawnTileY; }

	std::shared_ptr<Level> GetParentLevel() const { return ParentLevel.lock(); }

private:
	/** 소속 Level. 수명 순환을 피하기 위해 weak_ptr 로 보유. */
	std::weak_ptr<Level> ParentLevel;

	/** Level 내부에서 유일한 포탈 식별자. */
	uint64 PortalID = 0;

	/** 접촉 시 이동할 대상 Level 인덱스. */
	int32 TargetLevelID = -1;

	/** 대상 Level 에서 플레이어가 스폰될 타일 좌표. 포탈 타일과 반드시 다른 좌표여야 재진입 루프가 나지 않는다. */
	int32 TargetSpawnTileX = 0;
	int32 TargetSpawnTileY = 0;
};