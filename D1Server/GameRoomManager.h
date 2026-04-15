#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Core/Types.h"
#include "GameRoom.h"

namespace D1
{
	class UCollisionMap;

	/**
	 * 고정 4개 GameRoom 인스턴스를 보유하고 PlayerID Round-robin 할당을 담당하는 싱글톤.
	 *
	 * PlayerID 는 전역 atomic 으로 1부터 순차 발급되고, PlayerID % ROOM_COUNT 로 방을 결정한다.
	 * CollisionMap 은 1개 인스턴스를 4개 방이 공유한다(읽기 전용 — 스레드 안전).
	 */
	class GameRoomManager
	{
	public:
		/** 고정 방 개수 — FlushWorker 3개와 맞추어 최대 3방이 동시 Flush 가능. */
		static constexpr int32 ROOM_COUNT = 4;

		static GameRoomManager& GetInstance();

		/**
		 * CollisionMap 을 1회 로드하고 4개 GameRoom 인스턴스를 생성·초기화한다.
		 *
		 * @param CollisionCsvPath  Collision CSV 경로 (서버 exe 기준 상대/절대 경로)
		 * @return                  CSV 로드 성공 여부. 실패 시 TryMove 는 맵 경계만 체크한다.
		 */
		bool Initialize(const std::string& CollisionCsvPath);

		/**
		 * 전역 PlayerID 를 원자적으로 발급한 뒤 PlayerID % ROOM_COUNT 로 방을 결정하고 입장시킨다.
		 *
		 * @param Session     입장하는 세션
		 * @param OutRoomID   할당된 방 인덱스 [0, ROOM_COUNT)
		 * @param OutTileX    발급된 스폰 타일 X
		 * @param OutTileY    발급된 스폰 타일 Y
		 * @param OutOthers   해당 방의 기존 플레이어 스냅샷
		 * @return            발급된 전역 PlayerID (>= 1)
		 */
		uint64 EnterAnyRoom(
			const std::shared_ptr<GameServerSession>& Session,
			int32& OutRoomID,
			int32& OutTileX,
			int32& OutTileY,
			std::vector<GameRoom::PlayerEntry>& OutOthers);

		/** RoomID [0, ROOM_COUNT) 로 방을 조회한다. */
		std::shared_ptr<GameRoom>& GetRoom(int32 RoomID);

		/** PlayerID % ROOM_COUNT 로 방을 조회한다. */
		std::shared_ptr<GameRoom>& GetRoomByPlayerID(uint64 PlayerID) { return GetRoom(static_cast<int32>(PlayerID % ROOM_COUNT)); }

	private:
		GameRoomManager() = default;

		std::array<std::shared_ptr<GameRoom>, ROOM_COUNT> Rooms;
		std::shared_ptr<const UCollisionMap> SharedCollision;

		/** 전역 PlayerID 발급 카운터. 0 은 '미입장' 예약값이므로 1부터 시작. */
		std::atomic<uint64> NextPlayerID{1};
	};
}