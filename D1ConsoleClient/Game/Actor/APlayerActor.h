#pragma once

#include "AActor.h"
#include "../FVector2D.h"
#include "../Sprite.h"

namespace D1
{
	/**
	 * 플레이어가 조종하는 액터.
	 * WASD 입력에 따라 타일 단위로 이동하며, 렌더링 위치는 부드럽게 보간된다.
	 * Sprite를 통해 Idle/Walk 애니메이션을 재생하고, 좌향 이동 시 수평 반전한다.
	 */
	class APlayerActor : public AActor
	{
	public:
		APlayerActor();

		void Tick(float DeltaTime) override;
		void Render(HDC BackDC) override;

	private:
		/** 이동 중이 아닐 때 입력을 읽어 다음 목표 타일과 방향을 설정한다. */
		void ProcessInput();

		/** 렌더링 위치를 목표 픽셀 위치로 보간한다. */
		void UpdateMovement(float DeltaTime);

		// ---------------------------------------------------------------
		// 이동
		// ---------------------------------------------------------------

		/** 현재 목표 타일 좌표 */
		FVector2D TilePos;

		/** 픽셀 단위 목표 위치 (TilePos * TileSize) */
		FVector2D TargetPos;

		/** 타일 간 이동이 진행 중인지 */
		bool bIsMoving = false;

		static constexpr int32 TileSize  = 32;
		/** 스프라이트 화면 출력 크기 (TileSize의 2배 → 32×32 시트를 64×64로 확대 출력). */
		static constexpr int32 RenderSize = 64;
		static constexpr float MoveSpeed = 260.f; // 픽셀/초

		// ---------------------------------------------------------------
		// 애니메이션
		// ---------------------------------------------------------------

		enum class EAnimClip { Idle = 0, Walk = 1 };

		Sprite PlayerSprite;

		/** A키(좌향) 이동 시 true — Sprite::Render에 수평 반전 전달 */
		bool bFacingLeft = false;

		// 스프라이트 시트 레이아웃 (Adventurer Sprite Sheet v1.6, 32×32 프레임)
		static constexpr int32 IdleRow    = 0;
		static constexpr int32 IdleFrames = 13;
		static constexpr float IdleFps    = 8.f;
		static constexpr int32 WalkRow    = 1;
		static constexpr int32 WalkFrames = 8;
		static constexpr float WalkFps    = 25.f;
	};
}