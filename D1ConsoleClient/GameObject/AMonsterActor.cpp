#include "AMonsterActor.h"

#include "Render/ResourceManager.h"
#include "World/UWorld.h"
#include "UCollisionMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace D1
{
	AMonsterActor::AMonsterActor(int32 InStartTileX, int32 InStartTileY)
	{
		// 타일 좌표와 픽셀 좌표를 함께 초기화
		TilePos = { InStartTileX, InStartTileY };
		TargetPos = { static_cast<float>(InStartTileX * TileSize), static_cast<float>(InStartTileY * TileSize) };
		X = TargetPos.X;
		Y = TargetPos.Y;

		// 스프라이트 초기화 — 텍스처는 LoadResources() 시점에 등록됨 (MiniGolemSprite)
		ActorSprite = std::make_shared<Sprite>();
		auto Texture = ResourceManager::Get().GetTexture(L"MiniGolemSprite");

		ActorSprite->Init(Texture, GolemFrameSize);
		ActorSprite->SetRenderSize(RenderSize);

		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { IdleClip.Row,   IdleClip.Frames,   IdleClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { WalkClip.Row,   WalkClip.Frames,   WalkClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { AttackClip.Row, AttackClip.Frames, AttackClip.Fps });

		ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));
	}

	void AMonsterActor::Tick(float DeltaTime)
	{
		// Player/CollisionMap 조회 — 월드가 없거나 Player가 없으면 Idle 유지
		APlayerActor* Player = nullptr;
		std::shared_ptr<UCollisionMap> CollisionMap;
		if (World)
		{
			CollisionMap = World->GetCollisionMap();
			// 간단 검색: Actors 중 첫 번째 APlayerActor를 타겟으로 사용 (1마리 고정 스폰이므로 충분)
			for (const std::shared_ptr<AActor>& Actor : World->GetActorsForIteration())
			{
				if (auto* AsPlayer = dynamic_cast<APlayerActor*>(Actor.get()))
				{
					Player = AsPlayer;
					break;
				}
			}
		}

		// 1. 공격 타이머 진행 — 공격 중에는 이동/경로재계산을 중단
		if (bIsAttacking)
		{
			AttackTimer += DeltaTime;
			if (AttackTimer >= AttackDuration)
			{
				bIsAttacking = false;
				AttackTimer = 0.f;
			}

			// 공격 1사이클이 끝나기 전이라도 Player가 공격 범위 밖으로 벗어나면 즉시 공격을 취소한다.
			// 이 처리가 없으면 Player가 빠르게 도망쳐도 AttackDuration이 끝날 때까지 공격 상태가 유지되어
			// 범위 밖에서 공격이 지속되는 현상이 발생한다.
			if (bIsAttacking && Player)
			{
				const FTileCoord PlayerTile { Player->GetTileX(), Player->GetTileY() };
				if (ManhattanDistance(TilePos, PlayerTile) > AttackRangeTiles)
				{
					bIsAttacking = false;
					AttackTimer = 0.f;
				}
			}
		}

		// 2. 공격 판정 & 경로 관리 — 이동 보간 중이 아니고 공격 중이 아닐 때만.
		// 보간 중(bIsMoving=true)에는 TilePos가 "다음 도착 예정 타일"로 선반영되어 X/Y와 어긋나 있으므로,
		// 도착(bIsMoving=false) 이후에만 인접/경로 판정을 수행해 TilePos와 렌더 위치의 정합을 유지한다.
		if (!bIsAttacking && !bIsMoving && Player && CollisionMap)
		{
			const FTileCoord PlayerTile { Player->GetTileX(), Player->GetTileY() };

			// 인접 시 이동 중단 + Attack 진입 (쿨다운 없이 거리 유지되는 동안 재발사)
			if (ManhattanDistance(TilePos, PlayerTile) <= AttackRangeTiles)
			{
				// 이동 중이었다면 현재 타일에 스냅(다음 타일 도달 후 공격이 자연스러움) — 간단 정책: 이동 플래그만 해제
				bIsMoving = false;
				CurrentPath.clear();
				bIsAttacking = true;
				AttackTimer = 0.f;
			}
			else
			{
				// 경로 재계산 조건:
				//  (a) 현재 타일 도착(=bIsMoving false) 이고 CurrentPath가 비어있다
				//  (b) Player 타일이 마지막 계산 이후 바뀌었다
				const bool bPlayerMoved = (PlayerTile != LastKnownPlayerTile);
				const bool bNeedPath = (!bIsMoving && CurrentPath.empty());
				if (bPlayerMoved || bNeedPath)
				{
					RecalculatePath(*Player, *CollisionMap);
					LastKnownPlayerTile = PlayerTile;
				}

				// 3. 이동 중이 아니고 경로가 있으면 다음 타일을 목표로 설정
				if (!bIsMoving && !CurrentPath.empty())
				{
					const FTileCoord Next = CurrentPath.front();

					// 다음 타일이 다른 bBlocksMovement 액터에 점유되어 있으면 진입하지 않는다.
					// 경로를 무효화해 다음 Tick에서 재계산되도록 한다 (플레이어가 예상 경로 위로 이동한 경우 등).
					if (!World->CanMoveTo(this, Next.X, Next.Y))
					{
						CurrentPath.clear();
					}
					else
					{
						CurrentPath.erase(CurrentPath.begin());

						// 방향 반전(좌향) 판단: 새 타일이 현재보다 좌측에 있으면 flip
						if (Next.X < TilePos.X) bFacingLeft = true;
						else if (Next.X > TilePos.X) bFacingLeft = false;

						TilePos = Next;
						TargetPos = { static_cast<float>(Next.X * TileSize), static_cast<float>(Next.Y * TileSize) };
						bIsMoving = true;
					}
				}
			}
		}

		// 4. 이동 보간
		UpdateMovement(DeltaTime);

		// 5. 상태 우선순위: Attack > Walk > Idle
		EAnimClip CurrentClip = EAnimClip::Idle;
		if (bIsAttacking) CurrentClip = EAnimClip::Attack;
		else if (bIsMoving) CurrentClip = EAnimClip::Walk;

		if (ActorSprite)
			ActorSprite->SetClipId(static_cast<int32>(CurrentClip));

		// 6. Sprite 프레임 진행 (AnimActor::Tick 위임)
		AnimActor::Tick(DeltaTime);
	}
	
	void AMonsterActor::RecalculatePath(const APlayerActor& PlayerActor, const UCollisionMap& CollisionMap)
    {
        CurrentPath.clear();
        const FTileCoord Start = TilePos;
        const FTileCoord Goal { PlayerActor.GetTileX(), PlayerActor.GetTileY() };

        // Goal이 Start와 동일하거나 Goal 자체가 차단된 타일이면 경로 없음
        if (Start == Goal)
            return;
        // Player가 서있는 타일은 IsBlocked일 가능성은 낮지만 방어적으로 허용
        // (IsBlocked 체크는 이웃 확장 시 수행)

        FindPathAStar(Start, Goal, CollisionMap);
    }
    
    bool AMonsterActor::FindPathAStar(const FTileCoord& Start, const FTileCoord& Goal, const UCollisionMap& CollisionMap)
    {
        // A* 구현 (Manhattan heuristic, 4방향). 작은 그리드 기준 단순 벡터 기반 Open/Closed.
        // 대규모 확장 시에는 priority_queue + 해시 기반 g-score 맵이 더 효율적.

        /** 탐색 중 기록하는 노드 — 부모 인덱스를 통해 경로 역추적 */
        struct FNode
        {
            FTileCoord Tile;
            int32 ParentIndex; // AllNodes 내 인덱스 또는 -1 (Start)
            int32 G;           // Start에서 여기까지 실비용(타일 수)
            int32 F;           // G + H(Manhattan)
            bool bInOpen;      // Open 목록 소속 여부 (Closed로 이동하면 false)
        };

        std::vector<FNode> AllNodes;
        AllNodes.reserve(MaxSearchNodes);

        // 1) Start 노드 초기화
        FNode StartNode;
        StartNode.Tile = Start;
        StartNode.ParentIndex = -1;
        StartNode.G = 0;
        StartNode.F = ManhattanDistance(Start, Goal);
        StartNode.bInOpen = true;
        AllNodes.push_back(StartNode);

        const int32 Dx[4] = { 0, 0, -1, 1 };
        const int32 Dy[4] = { -1, 1, 0, 0 };

        int32 SearchCount = 0;
        while (SearchCount < MaxSearchNodes)
        {
            // 2a) Open 중 F 최소 노드 찾기 (선형 탐색 — MVP 단순 구현)
            int32 CurrentIdx = -1;
            int32 MinF = (std::numeric_limits<int32>::max)();
            for (int32 i = 0; i < static_cast<int32>(AllNodes.size()); ++i)
            {
                if (AllNodes[i].bInOpen && AllNodes[i].F < MinF)
                {
                    MinF = AllNodes[i].F;
                    CurrentIdx = i;
                }
            }

            // Open이 비어 있으면 경로 없음
            if (CurrentIdx == -1)
                return false;

            FNode Current = AllNodes[CurrentIdx];

            // 2b) Goal 도달 시 부모 체인 역추적
            if (Current.Tile == Goal)
            {
                // 역추적 — Start(부모 == -1)는 제외, Goal부터 Start 다음까지 쌓아 올려 역순으로 뒤집는다.
                std::vector<FTileCoord> Reversed;
                int32 Walk = CurrentIdx;
                while (AllNodes[Walk].ParentIndex != -1)
                {
                    Reversed.push_back(AllNodes[Walk].Tile);
                    Walk = AllNodes[Walk].ParentIndex;
                }
                // 현재 정책: Goal 타일(=Player 위치)까지 걷지 말고 그 직전 타일에서 멈춰 공격.
                // 그러나 AttackRangeTiles == 1 이므로 몬스터 Tick이 인접 도달 시점에 공격 분기로 빠진다.
                // 여기서는 Goal 포함 경로를 그대로 넣고, 실제 진입 여부는 Tick의 AttackRange 체크로 차단된다.
                CurrentPath.assign(Reversed.rbegin(), Reversed.rend());
                return true;
            }

            // 2c) Current를 Closed로 이동
            AllNodes[CurrentIdx].bInOpen = false;
            ++SearchCount;

            // 2d) 4방향 이웃 검사
            for (int32 d = 0; d < 4; ++d)
            {
                const FTileCoord Neighbor { Current.Tile.X + Dx[d], Current.Tile.Y + Dy[d] };

                // Goal 자체는 Player가 서있는 타일이므로 IsBlocked에 걸리지 않지만,
                // 만약 Player가 차단 타일에 있으면(이상 상황) 여전히 목표로 인정하기 위해 분기 처리.
                const bool bIsGoal = (Neighbor == Goal);
                if (!bIsGoal && CollisionMap.IsBlocked(Neighbor.X, Neighbor.Y))
                    continue;

                // 이미 방문(혹은 Open에 존재)한 노드 찾기 — 선형 탐색
                int32 ExistingIdx = -1;
                for (int32 i = 0; i < static_cast<int32>(AllNodes.size()); ++i)
                {
                    if (AllNodes[i].Tile == Neighbor)
                    {
                        ExistingIdx = i;
                        break;
                    }
                }

                const int32 TentativeG = Current.G + 1;

                if (ExistingIdx == -1)
                {
                    // 신규 노드 — Open에 추가
                    if (static_cast<int32>(AllNodes.size()) >= MaxSearchNodes)
                        continue;
                    FNode NewNode;
                    NewNode.Tile = Neighbor;
                    NewNode.ParentIndex = CurrentIdx;
                    NewNode.G = TentativeG;
                    NewNode.F = TentativeG + ManhattanDistance(Neighbor, Goal);
                    NewNode.bInOpen = true;
                    AllNodes.push_back(NewNode);
                }
                else
                {
                    // 기존 노드 — 더 짧은 경로를 발견했으면 부모/비용 갱신 후 Open에 재삽입
                    FNode& Existing = AllNodes[ExistingIdx];
                    if (TentativeG < Existing.G)
                    {
                        Existing.ParentIndex = CurrentIdx;
                        Existing.G = TentativeG;
                        Existing.F = TentativeG + ManhattanDistance(Neighbor, Goal);
                        Existing.bInOpen = true;
                    }
                }
            }
        }

        // 3) MaxSearchNodes 초과 — 경로 없음으로 처리
        return false;
    }

	void AMonsterActor::UpdateMovement(float DeltaTime)
	{
		if (!bIsMoving)
			return;

		FVector2D RenderPos = { X, Y };
		FVector2D Delta = TargetPos - RenderPos;

		float DistSq = Delta.LengthSquared();
		float Step = MoveSpeed * DeltaTime;

		if (Step * Step >= DistSq)
		{
			// 목표 타일 도달
			X = TargetPos.X;
			Y = TargetPos.Y;
			bIsMoving = false;
		}
		else
		{
			X += Delta.Normalized().X * Step;
			Y += Delta.Normalized().Y * Step;
		}
	}

	void AMonsterActor::Render(HDC BackDC)
	{
		// Player와 동일한 정렬 규약: 가로 중앙 + 발끝을 타일 하단에 맞춤
		constexpr int32 OffsetX = (TileSize - RenderSize) / 2;
		constexpr int32 OffsetY = TileSize - RenderSize;
		if (ActorSprite)
			ActorSprite->Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, bFacingLeft);
	}

	int32 AMonsterActor::ManhattanDistance(const FTileCoord& A, const FTileCoord& B)
	{
		return std::abs(A.X - B.X) + std::abs(A.Y - B.Y);
	}
}