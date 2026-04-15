#pragma once

#include "AnimActor.h"
#include "APlayerActor.h"
#include "Core/FVector2D.h"

#include <memory>
#include <vector>

namespace D1
{
	class UCollisionMap;

	/**
	 * 타일 좌표 하나를 표현하는 경량 구조체.
	 * A* 경로 결과 저장/비교에 사용한다 (FVector2D는 float라 == 연산에 부적합).
	 */
	struct FTileCoord
	{
		int32 X = 0;
		int32 Y = 0;

		bool operator==(const FTileCoord& Other) const { return X == Other.X && Y == Other.Y; }
		bool operator!=(const FTileCoord& Other) const { return !(*this == Other); }
	};

	/**
	 * 클라이언트 단독으로 동작하는 몬스터 액터 (Mini Golem).
	 * A*로 Player 타일을 추적하며, 맨해튼 거리 1칸이 되면 Attack 애니메이션을 재생한다.
	 * 서버 권위 없음(MVP). 체력/데미지 인프라 없음(애니메이션만).
	 */
	class AMonsterActor : public AnimActor
	{
	public:
		/**
		 * @param InStartTileX  스폰 타일의 X 좌표
		 * @param InStartTileY  스폰 타일의 Y 좌표
		 */
		AMonsterActor(int32 InStartTileX, int32 InStartTileY);

		void Tick(float DeltaTime) override;
		void Render(HDC BackDC) override;

		/** 논리 타일 좌표. 이동 보간 중에도 TilePos는 다음 도착 예정 타일이다. */
		int32 GetTileX() const override { return TilePos.X; }
		int32 GetTileY() const override { return TilePos.Y; }

	private:
		/** 타일 간 이동 보간 진행. 목표 타일 도달 시 bIsMoving = false로 만든다. */
		void UpdateMovement(float DeltaTime);

		/** Player의 현재 타일을 구해 A*로 경로를 갱신한다. 실패 시 CurrentPath는 비어있게 된다. */
		void RecalculatePath(const APlayerActor& PlayerActor, const UCollisionMap& CollisionMap);

		/**
		 * A* 경로탐색. Start → Goal 최단 경로를 CurrentPath(역순 아닌 Start 다음 타일부터 Goal까지)에 기록한다.
		 *
		 * 단계:
		 *  1) Open/Closed 리스트 초기화, Start 노드 생성하여 Open에 삽입.
		 *  2) Open이 빌 때까지 또는 MaxSearchNodes 초과 시까지 반복:
		 *     a) Open 중 f = g + h가 최소인 노드 Current를 꺼낸다.
		 *     b) Current == Goal 이면 부모 체인을 역추적하여 CurrentPath 구성 후 종료.
		 *     c) Current를 Closed에 추가하고, 4방향 이웃을 검사.
		 *     d) 이웃이 passable(!IsBlocked) 이고 Closed에 없으면 g/f를 계산, Open에 새로 넣거나 기존보다 g가 낮으면 갱신.
		 *  3) 경로를 찾지 못하면 CurrentPath는 비운다.
		 */
		bool FindPathAStar(const FTileCoord& Start, const FTileCoord& Goal, const UCollisionMap& CollisionMap);

		/** Current 타일에서 Player 타일까지 맨해튼 거리. 공격 판정/휴리스틱에 공통으로 쓴다. */
		static int32 ManhattanDistance(const FTileCoord& A, const FTileCoord& B);

		// ---------------------------------------------------------------
		// 위치/이동
		// ---------------------------------------------------------------

		/** 논리 타일 좌표(정수). 이동 중이면 "다음 도착 예정" 타일. */
		FTileCoord TilePos;

		/** 보간 목표 픽셀 좌표 (TilePos * TileSize) */
		FVector2D TargetPos;

		/** 현재 타일 간 이동 보간 진행 중인지 */
		bool bIsMoving = false;

		/** A* 결과: 다음 목표 타일들(FIFO 순서). 비어있으면 대기. */
		std::vector<FTileCoord> CurrentPath;

		/** 마지막으로 A*를 계산할 때 관측한 Player 타일. 이 값이 바뀌면 재계산. */
		FTileCoord LastKnownPlayerTile { -1, -1 };

		// ---------------------------------------------------------------
		// 애니메이션
		// ---------------------------------------------------------------

		enum class EAnimClip { Idle = 0, Walk = 1, Attack = 2 };

		/** 좌향 이동 시 수평 반전 */
		bool bFacingLeft = false;

		/** 공격 애니메이션 재생 중인지 */
		bool bIsAttacking = false;

		/** 공격 시작 이후 누적 시간(초) */
		float AttackTimer = 0.f;

		// ---------------------------------------------------------------
		// 튜닝값 (매직 넘버 금지 — 전부 named constant)
		// ---------------------------------------------------------------

		/** 타일 픽셀 크기 (Player와 동일 규약) */
		static constexpr int32 TileSize = 32;

		/** 스프라이트 화면 출력 크기 */
		static constexpr int32 RenderSize = 64;

		/** 이동 속도 (픽셀/초). Player보다 약간 느리게 설정. */
		static constexpr float MoveSpeed = 50.f;

		/** Player와 맨해튼 거리 이 값 이하이면 공격 범위. 1 = 바로 인접. */
		static constexpr int32 AttackRangeTiles = 1;

		/** A* 최대 탐색 노드 수. 경계 없는 루프 방지. */
		static constexpr int32 MaxSearchNodes = 4096;

		/**
		 * Mini Golem 스프라이트 클립 설정.
		 * TODO: 실제 시트 확인 후 Row/Frames/Fps 조정 필요.
		 * 현재 값은 합리적 기본값 — 시트가 32px 프레임이고 Idle=row0, Walk=row1, Attack=row2라고 가정.
		 */
		static constexpr FSpriteClipInfo IdleClip   = { 0, 4, 6.f };
		static constexpr FSpriteClipInfo WalkClip   = { 1, 6, 10.f };
		static constexpr FSpriteClipInfo AttackClip = { 2, 7, 12.f };

		/** Mini Golem 시트의 프레임 픽셀 크기 (정사각형 가정). TODO: 실제 시트에 맞춰 조정. */
		static constexpr int32 GolemFrameSize = 32;

		/** 공격 1사이클 재생 시간(초). 이 시간 이후 Idle/Walk로 복귀하거나 인접 유지 시 재공격 루프. */
		static constexpr float AttackDuration = static_cast<float>(AttackClip.Frames) / AttackClip.Fps;
	};
}