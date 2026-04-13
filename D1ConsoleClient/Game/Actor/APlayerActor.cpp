#include "APlayerActor.h"

#include "../../Subsystems/InputManager.h"

namespace D1
{
	APlayerActor::APlayerActor()
	{
		// 아레나 내부 시작 위치 (타일 2, 2)
		TilePos = { 2.f, 2.f };
		TargetPos = TilePos * static_cast<float>(TileSize);
		X = TargetPos.X;
		Y = TargetPos.Y;
	}

	void APlayerActor::Tick(float DeltaTime)
	{
		if (!bIsMoving)
			ProcessInput();
		UpdateMovement(DeltaTime);
	}

	void APlayerActor::ProcessInput()
	{
		InputManager& Input = InputManager::Get();

		FVector2D Dir = FVector2D::Zero();
		if (Input.GetKey(EKey::W)) Dir.Y = -1.f;
		else if (Input.GetKey(EKey::S)) Dir.Y = 1.f;
		else if (Input.GetKey(EKey::A)) Dir.X = -1.f;
		else if (Input.GetKey(EKey::D)) Dir.X = 1.f;

		if (Dir.LengthSquared() > 0.f)
		{
			TilePos += Dir;
			TargetPos = TilePos * static_cast<float>(TileSize);
			bIsMoving = true;
		}
	}

	void APlayerActor::UpdateMovement(float DeltaTime)
	{
		if (!bIsMoving)
			return;

		FVector2D RenderPos = { X, Y };
		FVector2D Delta = TargetPos - RenderPos;
		float Dist = Delta.Length();
		float Step = MoveSpeed * DeltaTime;

		if (Step >= Dist)
		{
			// 목표 도달: 위치 확정 및 이동 종료
			X = TargetPos.X;
			Y = TargetPos.Y;
			bIsMoving = false;
		}
		else
		{
			// 목표 방향으로 Step만큼 전진
			X += Delta.Normalized().X * Step;
			Y += Delta.Normalized().Y * Step;
		}
	}

	void APlayerActor::Render(HDC BackDC)
	{
		RECT Rect = {
			static_cast<LONG>(X),
			static_cast<LONG>(Y),
			static_cast<LONG>(X) + TileSize,
			static_cast<LONG>(Y) + TileSize
		};
		HBRUSH Brush = ::CreateSolidBrush(Color);
		::FillRect(BackDC, &Rect, Brush);
		::DeleteObject(Brush);
	}
}