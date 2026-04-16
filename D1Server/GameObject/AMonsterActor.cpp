#include "GameObject/AMonsterActor.h"
#include "UCollisionMap.h"
#include <climits>
#include <functional>
#include <queue>
#include <unordered_map>
#include "World/Level.h"

void AMonsterActor::ServerTick(int32 DeltaMs)
{

	// TODO : Tick 단계를 3개로 나누어 함수로 만들 것  
	// TODO : IDLETICK
	if (GetParentLevel()->IsPlayerEmpty())
	{
		State = EMonsterState::Idle;
		return;
	}

	// TODO : MOVETICK
	// 1. 타겟 결정 — 락온된 타겟이 있으면 유지, 없으면 가장 가까운 플레이어로 락온
	// LockedTargetID 는 OnPlayerLeft 에 의해 플레이어 퇴장 시 0으로 리셋된다.
	FTileNode TargetTile { -1, -1 };
	uint64 TargetID = 0;
	
	if (LockedTargetID != 0)
	{
		// LockedTargetID에 해당하는 Player가 없을경우 false
		if (GetParentLevel()->CalculatePlayerTileByID(LockedTargetID, TargetTile) == false)
		{
			LockedTargetID = 0;			
		}
	}
	else
	{
		if (GetParentLevel()->FindNearestPlayer(FTileNode{TileX, TileY}, TargetID, TargetTile))
		{
			LockedTargetID = TargetID;
			
			// 새로운 플레이어 찾았으니까
			CurrentPath.clear();
			LastKnownTargetTile = { -1, -1 };
		}
	}

	const FTileNode MyTile { TileX, TileY };
	const int32 Dist = ManhattanDistance(MyTile, TargetTile);

	// TODO : ATTACKTICK
	// 2. 공격/이동 쿨다운 감소
	if (AttackCooldownMs > 0)
		AttackCooldownMs -= DeltaMs;

	if (MoveCooldownMs > 0)
		MoveCooldownMs -= DeltaMs;

	// 3. 공격 범위 판정 — 인접하면 이동 중단 후 공격 이벤트 발생
	if (Dist <= AttackRangeTiles)
	{
		State = EMonsterState::Attack;
		CurrentPath.clear();

		if (AttackCooldownMs <= 0)
		{
			AttackCooldownMs = AttackCooldownTotalMs;
			GetParentLevel()->BroadcastMonsterAttack(MonsterID, LockedTargetID);
		}
		return;
	}

	// 4. 경로 재계산 조건: 타겟이 움직였거나 경로가 비어있음
	const bool bTargetMoved = (TargetTile != LastKnownTargetTile);
	if (bTargetMoved || CurrentPath.empty())
	{
		CurrentPath.clear();
		FindPathAStar(MyTile, TargetTile, CurrentPath);
		LastKnownTargetTile = TargetTile;
	}

	// 5. 경로의 첫 번째 타일로 한 칸 이동 (이동 쿨다운이 끝난 경우에만)
	if (CurrentPath.empty())
	{
		State = EMonsterState::Idle;
		return;
	}

	if (MoveCooldownMs > 0)
	{
		// 이동 대기 중 — 상태는 Move 유지하지만 좌표 갱신 없이 다음 Tick 대기
		State = EMonsterState::Move;
		return;
	}

	const FTileNode Next = CurrentPath.front();
	CurrentPath.erase(CurrentPath.begin());
	TileX = Next.X;
	TileY = Next.Y;
	State = EMonsterState::Move;
	MoveCooldownMs = MoveCooldownTotalMs;

	// 움직였으니 BroadCast 진행
	GetParentLevel()->BroadcastMonsterMove(MonsterID, TileX, TileY);
}

bool AMonsterActor::FindPathAStar(const FTileNode& Start, const FTileNode& Goal, std::vector<FTileNode>& OutPath) const
{
	std::priority_queue<FTileNode, std::vector<FTileNode>, std::greater<FTileNode>> PriorityQueue;
	
	// 각 타일까지 Start 로부터의 알려진 최소 G값(실비용, 걸어온 타일 수).
	std::unordered_map<int64, int32> best;

	// 경로 역추적을 위한 부모 타일 기록
	std::unordered_map<int64, FTileNode> Parent;
	
	// 경로 역추적을 담을 vector
	std::vector<FTileNode> Reversed;

	const int32 Dx[4] = { 0, 0, -1, 1 };
	const int32 Dy[4] = { -1, 1, 0, 0 };
	
	// 타일 좌표를 단일 int64 키로 압축해 해시맵에서 사용한다
	auto TileKey = [](const FTileNode& T) -> int64 { return (static_cast<int64>(T.X) << 32) | static_cast<uint32>(T.Y); };	

	best[TileKey(Start)] = 0;
	PriorityQueue.push({ Start.X, Start.Y, ManhattanDistance(Start, Goal) });

	while (!PriorityQueue.empty())
	{
		// F값이 가장 낮은 탐색 후보를 꺼낸다
		const FTileNode CurrentNode = PriorityQueue.top();
		PriorityQueue.pop();

		// 1. 목표 타일 도달 → Parent map을 역추적해 경로 구성
		if (CurrentNode == Goal)
		{
			FTileNode Walk = Goal;
			
			while (Walk != Start)
			{
				// 좌표가 도착지부터 들어가니까
				Reversed.push_back(Walk);
				Walk = Parent[TileKey(Walk)];
			}
			
			// 도착지(rbegin: 마지막원소)부터 시작점(rend:처음 원소)까지 vector 삽입
			OutPath.assign(Reversed.rbegin(), Reversed.rend());
			return true;
		}

		// 2. Stale skip — pop 된 F값이 현재 best(G)+H 보다 크면 이미 더 짧은 경로로 갱신된 오래된 엔트리.
		//    best 는 G, CurrentNode.Cost 는 F 이므로 단위를 맞추기 위해 H 를 더해 비교한다.
		const int32 CurG = best[TileKey(CurrentNode)];
		const int32 CurF = CurG + ManhattanDistance(CurrentNode, Goal);
		if (CurrentNode.Cost > CurF)
			continue;

		// 3. 아니면 이웃 탐색
		for (int32 d = 0; d < 4; ++d)
		{
			const FTileNode NextNode { CurrentNode.X + Dx[d], CurrentNode.Y + Dy[d] };

			// 목표 타일(= 플레이어 위치)은 충돌 검사 예외
			const bool bIsGoal = (NextNode == Goal);
			if (bIsGoal == false)
			{
				auto ColMap = GetParentLevel()->GetCollisionMap().lock();
				if (ColMap == nullptr || ColMap->IsBlocked(NextNode.X, NextNode.Y))
					continue;
			}

			const int32 CurrentBestcost = best[TileKey(CurrentNode)] + 1;

			// 이웃 타일까지 알려진 최소 G 값을 조회. 기록이 없으면 INT_MAX 로 둔다.
			if (best.count(TileKey(NextNode)) > 0)
			{
				if (CurrentBestcost >= best[TileKey(NextNode)])
				{
					continue;
				}
			}
			
			// 더 짧은 경로를 발견 → best/Parent 갱신 후 PriorityQueue 에 push
			best[TileKey(NextNode)] = CurrentBestcost;
			Parent[TileKey(NextNode)] = CurrentNode;
			const int32 F = CurrentBestcost + ManhattanDistance(NextNode, Goal);
			PriorityQueue.push({ NextNode.X, NextNode.Y, F });
		}
	}

	// 경로 없음
	return false;
}

int32 AMonsterActor::ManhattanDistance(const FTileNode& A, const FTileNode& B)
{
	return std::abs(A.X - B.X) + std::abs(A.Y - B.Y);
}
