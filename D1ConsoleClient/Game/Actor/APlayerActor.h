#pragma once

#include "AActor.h"
#include "../FVector2D.h"

namespace D1
{
	/**
	 * 플레이어가 조종하는 액터.
	 * WASD 입력에 따라 타일 단위로 이동하며, 렌더링 위치는 부드럽게 보간된다.
	 */
	class APlayerActor : public AActor
	{
	public:
		APlayerActor();

		void Tick(float DeltaTime) override;
		void Render(HDC BackDC) override;

	private:
		/** 이동 중이 아닐 때 입력을 읽어 다음 목표 타일을 설정한다. */
		void ProcessInput();

		/** 렌더링 위치를 목표 픽셀 위치로 보간한다. */
		void UpdateMovement(float DeltaTime);

		/** 현재 목표 타일 좌표 */
		FVector2D TilePos;

		/** 픽셀 단위 목표 위치 (TilePos * TileSize) */
		FVector2D TargetPos;

		/** 타일 간 이동이 진행 중인지 */
		bool bIsMoving = false;

		static constexpr int32 TileSize  = 32;
		static constexpr float MoveSpeed = 192.f; // 픽셀/초 (타일당 약 0.17초)

		COLORREF Color = RGB(0, 120, 215);
	};
}