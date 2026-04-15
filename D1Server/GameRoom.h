#pragma once

// Core/Types.h 가 <windows.h> 를 끌어오는데 그 전에 WIN32_LEAN_AND_MEAN 을 켜두지 않으면
// 구버전 winsock.h 가 포함되어 뒤에 오는 winsock2.h 와 충돌한다.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Core/Types.h"
#include "Job/JobSerializer.h"
#include "UCollisionMap.h"
#include "Protocol.pb.h"

namespace D1
{
	class GameServerSession;

	/**
	 * 필드에 입장한 플레이어들의 현재 스냅샷을 보관하는 서버 측 Room.
	 *
	 * JobSerializer 를 상속하여 모든 상태 변경을 단일 Job 직렬 실행으로 보호한다.
	 * std::mutex 없이 Flush Worker 가 순차 실행하므로 경쟁 조건이 발생하지 않는다.
	 *
	 * 외부 API (Enter/Leave/TryMove/Broadcast) 는 PushJob 래핑 형태로 제공하고,
	 * 실제 상태 변경은 Do* private 메서드 안에서 수행한다.
	 *
	 * Enter 예외 처리:
	 *   Enter 는 PlayerID/스폰 좌표/기존 플레이어 목록을 즉시 반환해야 하므로
	 *   PushJob 으로 비동기화하기 어렵다. 세션 최초 입장은 연결 당 1회뿐이고
	 *   PlayerID 발급은 atomic 으로 처리되므로, Enter 는 동기 호출을 유지한다.
	 *   Players 맵 접근은 Enter 전용 Mutex(EnterMutex) 로만 보호하며,
	 *   이후 모든 경로는 JobSerializer 직렬화에 맡긴다.
	 */
	class GameRoom : public JobSerializer
	{
	public:
		struct PlayerEntry
		{
			uint64 PlayerID = 0;
			int32 TileX = 0;
			int32 TileY = 0;
			std::weak_ptr<GameServerSession> Session;
		};

		static std::shared_ptr<GameRoom>& Get();

		/**
		 * Room 을 초기화한다. 충돌 CSV를 로드해 서버 측 타일 차단 검사에 사용한다.
		 *
		 * @param CollisionCsvPath  Collision CSV 절대/상대 경로 (0=통행, 1=차단)
		 * @return                  로드 성공 여부. 실패하면 TryMove는 맵 경계만 체크한다.
		 */
		bool Initialize(const std::string& CollisionCsvPath);

		/**
		 * 플레이어를 Room 에 등록한다. PlayerID/스폰 좌표를 발급하고, 현재 필드의 다른 플레이어 스냅샷을 반환한다.
		 *
		 * 동기 호출 — 세션 최초 입장은 연결 당 1회이므로 PushJob 비동기화 불필요.
		 * PlayerID 는 atomic 발급, Players 맵 접근은 EnterMutex 로 보호.
		 *
		 * @param Session      입장하는 세션. PlayerID 는 이 함수 호출 후 세션에 저장해야 한다.
		 * @param OutTileX     발급된 스폰 타일 X
		 * @param OutTileY     발급된 스폰 타일 Y
		 * @param OutOthers    자신을 제외한 기존 플레이어 목록
		 * @return             발급된 PlayerID (>= 1)
		 */
		uint64 Enter(const std::shared_ptr<GameServerSession>& Session, int32& OutTileX, int32& OutTileY, std::vector<PlayerEntry>& OutOthers);

		/** PlayerID 로 플레이어를 제거한다. PushJob 래핑. */
		void Leave(uint64 PlayerID);

		/**
		 * 이동 요청을 권위적으로 검증하고 처리한다. PushJob 래핑.
		 * 실제 처리는 DoTryMove 에서 수행.
		 *
		 * @param PlayerID   이동을 요청한 플레이어 ID
		 * @param Dir        요청된 이동 방향
		 */
		void TryMove(uint64 PlayerID, Protocol::Direction Dir);

		/**
		 * 현재 등록된 모든 세션에 SendBuffer 를 전송한다. PushJob 래핑.
		 * 실제 처리는 DoBroadcast 에서 수행.
		 *
		 * @param Buffer     전송할 SendBuffer
		 * @param ExceptID   제외할 PlayerID (브로드캐스터 본인 제외). 0 이면 모두에게 전송.
		 */
		void Broadcast(SendBufferRef Buffer, uint64 ExceptID = 0);

	private:
		GameRoom() = default;

		/** Leave 내부 구현 — Job 직렬화 안에서 실행. lock 없음. */
		void DoLeave(uint64 PlayerID);

		/** TryMove 내부 구현 — Job 직렬화 안에서 실행. lock 없음. */
		void DoTryMove(uint64 PlayerID, Protocol::Direction Dir);

		/** Broadcast 내부 구현 — Job 직렬화 안에서 실행. lock 없음. */
		void DoBroadcast(SendBufferRef Buffer, uint64 ExceptID);

		/** 다음 발급 PlayerID. 0 은 '미입장'을 의미하므로 1부터 시작. */
		std::atomic<uint64> NextPlayerID{ 1 };

		/** Enter 동기 호출 구간의 Players 맵 보호용 뮤텍스. Do* 에서는 사용하지 않는다. */
		std::mutex EnterMutex;
		std::unordered_map<uint64, PlayerEntry> Players;

		/** 서버 측 충돌 맵. Initialize 에서 CSV로 1회 로드. */
		UCollisionMap CollisionMap;
		bool bCollisionLoaded = false;

		/** 스폰 시작 타일. 통행 가능하고 다른 액터와 겹치지 않는 기본 위치. */
		static constexpr int32 SpawnBaseTileX = 5;
		static constexpr int32 SpawnBaseTileY = 10;
		/** PlayerID 증가에 따라 X 방향으로 한 칸씩 밀어 단순한 고유 스폰을 보장한다. */
		static constexpr int32 SpawnStrideX = 1;
	};
}