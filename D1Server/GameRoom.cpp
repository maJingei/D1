#include "GameRoom.h"

#include "ClientPacketHandler.h"
#include "GameServerSession.h"
#include "Iocp/Session.h"
#include "Job/Job.h"
#include "Core/DiagCounters.h"

#include <iostream>
#include <chrono>

namespace D1
{
	JobSerializerRef GameRoom::GetSerializerRef()
	{
		// enable_shared_from_this<GameRoom> 로 자신을 JobSerializer 로 캐스트하여 반환한다.
		return std::static_pointer_cast<JobSerializer>(shared_from_this());
	}

	void GameRoom::Initialize(std::shared_ptr<const UCollisionMap> InCollision, int32 InRoomID)
	{
		CollisionMap = std::move(InCollision);
		bCollisionLoaded = (CollisionMap != nullptr);
		RoomID = InRoomID;
	}

	void GameRoom::EnterWithExistingID(
		uint64 PlayerID,
		const std::shared_ptr<GameServerSession>& Session,
		int32& OutTileX,
		int32& OutTileY,
		std::vector<PlayerEntry>& OutOthers)
	{
		// 스폰 좌표 계산 — 단순 오프셋. PlayerID 는 전역 유일이므로 방 내에서도 겹치지 않는다.
		// TODO: 겹침 처리 구현 필요
		OutTileX = SpawnBaseTileX + static_cast<int32>((PlayerID - 1) * SpawnStrideX);
		OutTileY = SpawnBaseTileY;

		PlayerEntry NewEntry;
		NewEntry.PlayerID = PlayerID;
		NewEntry.TileX = OutTileX;
		NewEntry.TileY = OutTileY;
		NewEntry.Session = Session;

		{
			// Enter 는 동기 호출이므로 EnterMutex 로 보호한다.
			// Do* 계열 함수는 Job 직렬화 안에서 실행되므로 이 락을 사용하지 않는다.
			std::lock_guard<std::mutex> Lock(EnterMutex);

			OutOthers.reserve(Players.size());
			for (const auto& Pair : Players)
				OutOthers.push_back(Pair.second);

			Players.emplace(PlayerID, std::move(NewEntry));
		}
	}

	void GameRoom::Leave(uint64 PlayerID)
	{
		PushJob(std::make_shared<Job>(shared_from_this(), &GameRoom::DoLeave, PlayerID));
	}

	void GameRoom::TryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq)
	{
		PushJob(std::make_shared<Job>(shared_from_this(), &GameRoom::DoTryMove, PlayerID, Dir, ClientSeq));
	}

	void GameRoom::Broadcast(SendBufferRef Buffer, uint64 ExceptID)
	{
		PushJob(std::make_shared<Job>(shared_from_this(), &GameRoom::DoBroadcast, Buffer, ExceptID));
	}

	void GameRoom::DoLeave(uint64 PlayerID)
	{
		Players.erase(PlayerID);
	}

	void GameRoom::DoTryMove(uint64 PlayerID, Protocol::Direction Dir, uint64 ClientSeq)
	{
		// [DIAG] probe: 방별 TryMove 호출 카운터
		if (RoomID >= 0 && RoomID < static_cast<int32>(DIAG_ROOM_COUNT))
			GRoomTryMoveCount[RoomID].fetch_add(1, std::memory_order_relaxed);

		// 방향 -> 델타 변환. 대각 없음.
		int32 DeltaX = 0;
		int32 DeltaY = 0;
		switch (Dir)
		{
		case Protocol::DIR_UP:    DeltaY = -1; break;
		case Protocol::DIR_DOWN:  DeltaY =  1; break;
		case Protocol::DIR_LEFT:  DeltaX = -1; break;
		case Protocol::DIR_RIGHT: DeltaX =  1; break;
		default:
			// 알 수 없는 방향은 거절. 현재 위치를 그대로 회신해 입력 락을 해제시킨다.
			break;
		}

		auto It = Players.find(PlayerID);
		if (It == Players.end())
			return;

		PlayerEntry& Self = It->second;
		std::shared_ptr<GameServerSession> RequesterSession = Self.Session.lock();

		const int32 NextTileX = Self.TileX + DeltaX;
		const int32 NextTileY = Self.TileY + DeltaY;

		// // 1. 타일맵 차단 검사 — CollisionMap 이 null 이면 차단 없음으로 처리한다.
		// const bool bTileBlocked = bCollisionLoaded && CollisionMap && CollisionMap->IsBlocked(NextTileX, NextTileY);
		// if (bTileBlocked)
		// {
		// 	// 실패: 요청자에게 현재 위치를 돌려 입력 락을 해제시킨다.
		// 	if (RequesterSession)
		// 	{
		// 		Protocol::S_MOVE_REJECT RejectPkt;
		// 		RejectPkt.set_player_id(PlayerID);
		// 		RejectPkt.set_tile_x(Self.TileX);
		// 		RejectPkt.set_tile_y(Self.TileY);
		// 		RejectPkt.set_client_seq(ClientSeq);
		// 		RequesterSession->Send(ClientPacketHandler::MakeSendBuffer(RejectPkt));
		// 	}
		// 	return;
		// }
		//
		// // 2. 다른 플레이어 점유 검사 — 자기 자신은 건너뛴다.
		// bool bOccupied = false;
		// for (const auto& Pair : Players)
		// {
		// 	if (Pair.first == PlayerID) continue;
		// 	const PlayerEntry& Other = Pair.second;
		// 	if (Other.TileX == NextTileX && Other.TileY == NextTileY)
		// 	{
		// 		bOccupied = true;
		// 		break;
		// 	}
		// }
		//
		// if (bOccupied || (DeltaX == 0 && DeltaY == 0))
		// {
		// 	// 실패: 요청자에게 현재 위치를 돌려준다.
		// 	if (RequesterSession)
		// 	{
		// 		Protocol::S_MOVE_REJECT RejectPkt;
		// 		RejectPkt.set_player_id(PlayerID);
		// 		RejectPkt.set_tile_x(Self.TileX);
		// 		RejectPkt.set_tile_y(Self.TileY);
		// 		RejectPkt.set_client_seq(ClientSeq);
		// 		RequesterSession->Send(ClientPacketHandler::MakeSendBuffer(RejectPkt));
		// 	}
		// 	return;
		// }

		// 3. 위치 갱신 — Job 직렬화 덕분에 락 없이 안전하게 수정.
		Self.TileX = NextTileX;
		Self.TileY = NextTileY;

		// 성공: 모든 세션에 S_MOVE 브로드캐스트. 요청자도 포함해야 이동 시작 시점이 일치한다.
		Protocol::S_MOVE MovePkt;
		MovePkt.set_player_id(PlayerID);
		MovePkt.set_tile_x(NextTileX);
		MovePkt.set_tile_y(NextTileY);
		MovePkt.set_dir(Dir);
		MovePkt.set_client_seq(ClientSeq);
		DoBroadcast(ClientPacketHandler::MakeSendBuffer(MovePkt), 0);
	}

	void GameRoom::DoBroadcast(SendBufferRef Buffer, uint64 ExceptID)
	{
		// 세션 참조를 먼저 수집한 뒤 Send 를 호출한다.
		// Send 는 각 Session 의 Serializer 에 Job 을 Push 하므로 즉시 반환된다.
		// 수집 후 일괄 전송으로 분리하여 Players 순회 중 상태 변경을 방지한다.
		std::vector<std::shared_ptr<GameServerSession>> Targets;
		Targets.reserve(Players.size());
		for (const auto& Pair : Players)
		{
			if (Pair.first == ExceptID) continue;
			if (auto Locked = Pair.second.Session.lock())
				Targets.push_back(Locked);
		}

		// [DIAG] probe: Targets 수집 이후 Send 루프 시간 측정 시작
		const std::chrono::steady_clock::time_point T0 = std::chrono::steady_clock::now();

		for (const auto& Session : Targets)
			Session->Send(Buffer);

		// [DIAG] probe: Send 루프 종료 — ns 단위 누계
		const std::chrono::steady_clock::time_point T1 = std::chrono::steady_clock::now();
		const uint64 ElapsedNs = static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(T1 - T0).count());
		GDoBroadcastNsSum.fetch_add(ElapsedNs, std::memory_order_relaxed);
		GDoBroadcastCallCount.fetch_add(1, std::memory_order_relaxed);
		GDoBroadcastSessionSum.fetch_add(static_cast<uint64>(Targets.size()), std::memory_order_relaxed);
	}
}