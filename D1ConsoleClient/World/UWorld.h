#pragma once

#include "Core/CoreMinimal.h"
#include "LevelConfig.h"
#include <array>
#include <vector>
#include <memory>
#include <unordered_map>

#include "GameObject/APlayerActor.h"
#include "GameObject/AMonsterActor.h"

class AActor;
class UTileMap;
class UCollisionMap;

/** 단일 Level 의 정적 리소스 세트 — 시작 시 프리로드되어 CurrentLevelID 에 따라 선택적으로 렌더/충돌 판정된다. */
struct FLevelAssets
{
	std::vector<std::unique_ptr<UTileMap>> TileLayers;
	std::shared_ptr<UCollisionMap> CollisionMap;
};

/** 게임 오브젝트(AActor) 목록을 소유하고 Tick/Render를 위임하는 월드. */
class UWorld
{
public:
	UWorld() = default;
	~UWorld() = default;

	/** 매 프레임 등록된 모든 Actor의 Tick을 호출한다. */
	void Tick(float DeltaTime);

	/** 배경 TileMap을 먼저 그린 뒤, 등록된 모든 Actor의 Render를 호출한다. */
	void Render(HDC BackDC);

	/** 지정 LevelID 의 타일 레이어에 Ground→Water→Wall 순으로 TileMap 을 등록한다. */
	void AddTileLayer(int32 LevelID, std::unique_ptr<UTileMap> InTileMap);

	/** 지정 LevelID 의 충돌 맵을 주입한다. */
	void SetCollisionMap(int32 LevelID, std::shared_ptr<UCollisionMap> InCollisionMap);

	/** 현재 활성 Level 의 충돌 맵을 반환한다. 미주입 시 nullptr. */
	const std::shared_ptr<UCollisionMap>& GetCollisionMap() const { return PerLevel[CurrentLevelID].CollisionMap; }

	/**
	 * 화면/충돌 대상 Level 을 전환한다. 포탈 전이(S_PORTAL_TELEPORT) 시 호출된다.
	 * LevelID 범위 밖이면 무시(현재 Level 유지).
	 */
	void SetCurrentLevelID(int32 InLevelID);

	/** 현재 화면/충돌 판정 기준 Level ID. */
	int32 GetCurrentLevelID() const { return CurrentLevelID; }

	/** Actor를 생성하고 월드에 등록한다. Player/Monster 는 ID 맵에도 자동 등록하여 FindXxxActor 가 O(1) 조회되도록 한다. */
	template<typename T, typename... Args>
	std::shared_ptr<T> SpawnActor(Args&&... args)
	{
		std::shared_ptr<T> Actor = std::make_shared<T>(std::forward<Args>(args)...);
		Actor->SetWorld(this);
		Actors.push_back(Actor);

		// 매 패킷마다 반복 조회되는 Player/Monster 는 dynamic_cast + 선형 탐색을 피하기 위해 ID 맵에 별도 등록한다.
		// 파생 클래스(APlayerFemaleActor/APlayerDwarfActor 등) 로 스폰돼도 PlayerMap 에 등록되도록 is_base_of_v 로 확장.
		if constexpr (std::is_base_of_v<APlayerActor, T>)
			PlayerMap[Actor->GetPlayerID()] = Actor;
		else if constexpr (std::is_base_of_v<AMonsterActor, T>)
			MonsterMap[Actor->GetMonsterID()] = Actor;

		return Actor;
	}

	/** Actor를 월드에서 제거한다. shared_ptr 참조 카운트가 0이 되면 소멸된다. 맵의 weak_ptr 은 expire 로 자연 무효화. */
	void DestroyActor(const std::shared_ptr<AActor>& Actor);

	/** Actor 목록을 순회용으로 반환한다 (AI/대상 탐색 등 read-only 용도). */
	const std::vector<std::shared_ptr<AActor>>& GetActorsForIteration() const { return Actors; }

	/** ID 로 PlayerActor 를 찾는다. 맵에 없거나 이미 소멸된 경우 nullptr. */
	std::shared_ptr<APlayerActor> FindPlayerActor(uint64 PlayerID);

	/** ID 로 MonsterActor 를 찾는다. 맵에 없거나 이미 소멸된 경우 nullptr. */
	std::shared_ptr<AMonsterActor> FindMonsterActor(uint64 MonsterID);

	/** Mover가 (TileX, TileY) 타일로 이동 가능한지 검사한다. */
	bool CanMoveTo(const AActor* Mover, int32 TileX, int32 TileY) const;

private:
	/** Level 별 정적 리소스 세트. 인덱스 = LevelID. 시작 시 Game::LoadResources 가 LEVEL_COUNT 개 모두 프리로드한다. */
	std::array<FLevelAssets, LEVEL_COUNT> PerLevel;

	/** 현재 화면/충돌 판정 대상 LevelID. 포탈 전이 시 SetCurrentLevelID 로 갱신. */
	int32 CurrentLevelID = 0;

	/** 모든 액터(Player/Monster/Effect/Portal 등) 의 소유 컬렉션. Tick/Render 순회 대상. */
	std::vector<std::shared_ptr<AActor>> Actors;

	/** ID 기반 빠른 조회용 보조 맵 — weak_ptr 이므로 DestroyActor 후 자동 expire. */
	std::unordered_map<uint64, std::weak_ptr<APlayerActor>> PlayerMap;
	std::unordered_map<uint64, std::weak_ptr<AMonsterActor>> MonsterMap;
};
