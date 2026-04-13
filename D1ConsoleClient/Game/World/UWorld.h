#pragma once

#include <Windows.h>
#include <vector>
#include <memory>

namespace D1
{
	class AActor;
	class UTileMap;
	class UCollisionMap;

	/**
	 * 게임 오브젝트(AActor) 목록을 소유하고 Tick/Render를 위임하는 월드.
	 * Game이 UWorld를 소유하며, Actor의 생명주기를 관리한다.
	 */
	class UWorld
	{
	public:
		UWorld() = default;
		~UWorld() = default;

		/** 매 프레임 등록된 모든 Actor의 Tick을 호출한다. */
		void Tick(float DeltaTime);

		/**
		 * 배경 TileMap을 먼저 그린 뒤, 등록된 모든 Actor의 Render를 호출한다.
		 * TileMap이 없으면 Actor만 그린다.
		 */
		void Render(HDC BackDC);

		/** TileMap 레이어를 추가한다. 추가 순서대로 렌더링된다 (Ground → Wall → Water 등). */
		void AddTileLayer(std::unique_ptr<UTileMap> InTileMap);

		/** 충돌 맵을 주입한다. APlayerActor가 이동 전 IsBlocked 질의에 사용한다. */
		void SetCollisionMap(std::shared_ptr<UCollisionMap> InCollisionMap) { CollisionMap = std::move(InCollisionMap); }

		/** 주입된 충돌 맵을 반환한다. 미주입 시 nullptr. */
		const std::shared_ptr<UCollisionMap>& GetCollisionMap() const { return CollisionMap; }

		/**
		 * Actor를 생성하고 월드에 등록한다.
		 *
		 * @return  생성된 Actor의 shared_ptr
		 */
		template<typename T, typename... Args>
		std::shared_ptr<T> SpawnActor(Args&&... args)
		{
			std::shared_ptr<T> Actor = std::make_shared<T>(std::forward<Args>(args)...);
			Actor->SetWorld(this);
			Actors.push_back(Actor);
			return Actor;
		}

		/** Actor를 월드에서 제거한다. shared_ptr 참조 카운트가 0이 되면 소멸된다. */
		void DestroyActor(const std::shared_ptr<AActor>& Actor);

	private:
		std::vector<std::unique_ptr<UTileMap>> TileLayers;
		std::vector<std::shared_ptr<AActor>> Actors;

		/** 단일 충돌 레이어 (논리 전용, 렌더링 안 함) */
		std::shared_ptr<UCollisionMap> CollisionMap;
	};
}