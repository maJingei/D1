#include "GameRoom.h"

#include "ClientPacketHandler.h"
#include "GameServerSession.h"
#include "Iocp/Session.h"
#include "Job/Job.h"

#include <iostream>

namespace D1
{
	std::shared_ptr<GameRoom>& GameRoom::Get()
	{
		// 로컬 정적 shared_ptr — JobSerializer::shared_from_this 가 동작하려면
		// GameRoom 이 반드시 shared_ptr 로 관리되어야 한다.
		static std::shared_ptr<GameRoom> Instance(new GameRoom());
		return Instance;
	}

	bool GameRoom::Initialize(const std::string& CollisionCsvPath)
	{
		bCollisionLoaded = CollisionMap.Load(CollisionCsvPath);
		return bCollisionLoaded;
	}

	uint64 GameRoom::Enter(const std::shared_ptr<GameServerSession>& Session, int32& OutTileX, int32& OutTileY, std::vector<PlayerEntry>& OutOthers)
	{
		// 1. PlayerID 선할당 — atomic 으로 락 바깥에서 부여. 충돌 없음.
		const uint64 NewID = NextPlayerID.fetch_add(1);

		// 2. 스폰 좌표 계산 — 단순 오프셋.
		// TODO: 겹침 처리 구현 필요
		OutTileX = SpawnBaseTileX + static_cast<int32>((NewID - 1) * SpawnStrideX);
		OutTileY = SpawnBaseTileY;

		// 3. 기존 목록 스냅샷 + 신규 등록
		PlayerEntry NewEntry;
		NewEntry.PlayerID = NewID;
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

			Players.emplace(NewID, std::move(NewEntry));
		}

		return NewID;
	}

	void GameRoom::Leave(uint64 PlayerID)
	{
		PushJob(std::make_shared<Job>(Get(), &GameRoom::DoLeave, PlayerID));
	}

	void GameRoom::TryMove(uint64 PlayerID, Protocol::Direction Dir)
	{
		PushJob(std::make_shared<Job>(Get(), &GameRoom::DoTryMove, PlayerID, Dir));
	}

	void GameRoom::Broadcast(SendBufferRef Buffer, uint64 ExceptID)
	{
		PushJob(std::make_shared<Job>(Get(), &GameRoom::DoBroadcast, Buffer, ExceptID));
	}

	void GameRoom::DoLeave(uint64 PlayerID)
	{
		Players.erase(PlayerID);
	}

	void GameRoom::DoTryMove(uint64 PlayerID, Protocol::Direction Dir)
	{
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

		// 1. 타일맵 차단 검사
		const bool bTileBlocked = bCollisionLoaded && CollisionMap.IsBlocked(NextTileX, NextTileY);
		if (bTileBlocked)
		{
			// 실패: 요청자에게 현재 위치를 돌려 입력 락을 해제시킨다.
			if (RequesterSession)
			{
				Protocol::S_MOVE_REJECT RejectPkt;
				RejectPkt.set_player_id(PlayerID);
				RejectPkt.set_tile_x(Self.TileX);
				RejectPkt.set_tile_y(Self.TileY);
				RequesterSession->Send(ClientPacketHandler::MakeSendBuffer(RejectPkt));
				std::cout << "[Server] MOVE reject id=" << PlayerID << " stay=(" << Self.TileX << "," << Self.TileY << ")" << std::endl;
			}
			return;
		}

		// 2. 다른 플레이어 점유 검사 — 자기 자신은 건너뛴다.
		bool bOccupied = false;
		for (const auto& Pair : Players)
		{
			if (Pair.first == PlayerID) continue;
			const PlayerEntry& Other = Pair.second;
			if (Other.TileX == NextTileX && Other.TileY == NextTileY)
			{
				bOccupied = true;
				break;
			}
		}

		if (bOccupied || (DeltaX == 0 && DeltaY == 0))
		{
			// 실패: 요청자에게 현재 위치를 돌려준다.
			if (RequesterSession)
			{
				Protocol::S_MOVE_REJECT RejectPkt;
				RejectPkt.set_player_id(PlayerID);
				RejectPkt.set_tile_x(Self.TileX);
				RejectPkt.set_tile_y(Self.TileY);
				RequesterSession->Send(ClientPacketHandler::MakeSendBuffer(RejectPkt));
				std::cout << "[Server] MOVE reject id=" << PlayerID << " stay=(" << Self.TileX << "," << Self.TileY << ")" << std::endl;
			}
			return;
		}

		// 3. 위치 갱신 — Job 직렬화 덕분에 락 없이 안전하게 수정.
		Self.TileX = NextTileX;
		Self.TileY = NextTileY;

		// 성공: 모든 세션에 S_MOVE 브로드캐스트. 요청자도 포함해야 이동 시작 시점이 일치한다.
		Protocol::S_MOVE MovePkt;
		MovePkt.set_player_id(PlayerID);
		MovePkt.set_tile_x(NextTileX);
		MovePkt.set_tile_y(NextTileY);
		MovePkt.set_dir(Dir);
		DoBroadcast(ClientPacketHandler::MakeSendBuffer(MovePkt), 0);
		std::cout << "[Server] MOVE accept id=" << PlayerID << " tile=(" << NextTileX << "," << NextTileY << ")" << std::endl;
	}

	void GameRoom::DoBroadcast(SendBufferRef Buffer, uint64 ExceptID)
	{
		// 세션 참조를 먼저 수집한 뒤 Send 를 호출한다.
		// Send 내부에 SendLock(ReadWriteLock) 이 있으므로 Players 순회 중 호출하면
		// 락 순서가 꼬일 수 있다 — 수집 후 일괄 전송으로 분리한다.
		std::vector<std::shared_ptr<GameServerSession>> Targets;
		Targets.reserve(Players.size());
		for (const auto& Pair : Players)
		{
			if (Pair.first == ExceptID) continue;
			if (auto Locked = Pair.second.Session.lock())
				Targets.push_back(Locked);
		}

		for (const auto& Session : Targets)
			Session->Send(Buffer);
	}
}