#include "GameRoomManager.h"

#include "GameRoom.h"
#include "UCollisionMap.h"

#include <iostream>

namespace D1
{
	GameRoomManager& GameRoomManager::GetInstance()
	{
		static GameRoomManager Instance;
		return Instance;
	}

	bool GameRoomManager::Initialize(const std::string& CollisionCsvPath)
	{
		// CollisionMap 을 1회 로드하여 4개 방이 공유한다.
		auto Collision = std::make_shared<UCollisionMap>();
		const bool bLoaded = Collision->Load(CollisionCsvPath);
		if (bLoaded)
		{
			SharedCollision = std::move(Collision);
			std::cout << "[GameRoomManager] CollisionMap loaded: " << CollisionCsvPath << "\n";
		}
		else
		{
			std::cout << "[GameRoomManager] CollisionMap load failed: " << CollisionCsvPath << "\n";
		}

		// 4개 GameRoom 인스턴스 생성 및 초기화 — CollisionMap 공유 포인터 주입.
		for (int32 i = 0; i < ROOM_COUNT; i++)
		{
			Rooms[i] = std::make_shared<GameRoom>();
			Rooms[i]->Initialize(SharedCollision, i);
		}

		std::cout << "[GameRoomManager] " << ROOM_COUNT << " rooms initialized.\n";
		return bLoaded;
	}

	uint64 GameRoomManager::EnterAnyRoom(
		const std::shared_ptr<GameServerSession>& Session,
		int32& OutRoomID,
		int32& OutTileX,
		int32& OutTileY,
		std::vector<GameRoom::PlayerEntry>& OutOthers)
	{
		// 1. 전역 PlayerID 원자적 발급 — 0 은 '미입장' 예약값이므로 1부터 시작.
		const uint64 NewID = NextPlayerID.fetch_add(1, std::memory_order_relaxed);

		// 2. Round-robin 방 결정 — PlayerID % ROOM_COUNT
		OutRoomID = static_cast<int32>(NewID % static_cast<uint64>(ROOM_COUNT));

		// 3. 해당 방에 입장 처리.
		Rooms[OutRoomID]->EnterWithExistingID(NewID, Session, OutTileX, OutTileY, OutOthers);

		return NewID;
	}

	std::shared_ptr<GameRoom>& GameRoomManager::GetRoom(int32 RoomID)
	{
		// 범위 초과 시 방 0 을 안전 폴백으로 반환한다.
		if (RoomID < 0 || RoomID >= ROOM_COUNT)
			return Rooms[0];
		return Rooms[RoomID];
	}
}