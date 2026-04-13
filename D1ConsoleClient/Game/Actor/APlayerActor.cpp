#include "APlayerActor.h"

#include "../../Subsystems/InputManager.h"
#include "../../Subsystems/ResourceManager.h"
#include "../World/UWorld.h"
#include "../World/UCollisionMap.h"

namespace D1
{
	APlayerActor::APlayerActor()
	{
		// 아레나 내부 시작 위치 (타일 2, 2)
		TilePos = { 5.f, 10.f };
		TargetPos = TilePos * static_cast<float>(TileSize);
		X = TargetPos.X;
		Y = TargetPos.Y;

		// 스프라이트 초기화 — 텍스처는 LoadResources() 이후 이미 등록되어 있다
		auto Texture = ResourceManager::Get().GetTexture(L"PlayerSprite");
		// 시트는 32×32 프레임이지만, 화면에는 RenderSize(=64)로 확대 출력한다.
		PlayerSprite.Init(Texture, TileSize);
		PlayerSprite.SetRenderSize(RenderSize);
		PlayerSprite.AddClip(static_cast<int32>(EAnimClip::Idle), { IdleRow, IdleFrames, IdleFps });
		PlayerSprite.AddClip(static_cast<int32>(EAnimClip::Walk), { WalkRow, WalkFrames, WalkFps });
		PlayerSprite.Play(static_cast<int32>(EAnimClip::Idle));
	}

	void APlayerActor::Tick(float DeltaTime)
	{
		if (!bIsMoving)
			ProcessInput();
		UpdateMovement(DeltaTime);

		// 이동 상태에 따라 클립 전환 후 프레임 진행
		int32 ClipId = bIsMoving
			? static_cast<int32>(EAnimClip::Walk)
			: static_cast<int32>(EAnimClip::Idle);
		PlayerSprite.Play(ClipId);
		PlayerSprite.Update(DeltaTime);
	}

	void APlayerActor::Render(HDC BackDC)
	{
		// 확대 스프라이트(RenderSize)를 타일 중앙(가로) + 발끝을 타일 하단에 정렬한다.
		// OffsetX는 음수: 가로 중앙을 맞추기 위해 좌측으로 (RenderSize-TileSize)/2 만큼 당김.
		// OffsetY는 음수: 발끝을 타일 하단에 맞추기 위해 위로 (RenderSize-TileSize) 만큼 끌어올림.
		constexpr int32 OffsetX = (TileSize - RenderSize) / 2;
		constexpr int32 OffsetY = TileSize - RenderSize;
		PlayerSprite.Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, bFacingLeft);
	}

	void APlayerActor::ProcessInput()
	{
		InputManager& Input = InputManager::Get();

		FVector2D Dir = FVector2D::Zero();
		if      (Input.GetKey(EKey::W)) Dir.Y = -1.f;
		else if (Input.GetKey(EKey::S)) Dir.Y = 1.f;
		else if (Input.GetKey(EKey::A)) { Dir.X = -1.f; bFacingLeft = true; }
		else if (Input.GetKey(EKey::D)) { Dir.X =  1.f; bFacingLeft = false; }

		if (Dir.LengthSquared() > 0.f)
		{
			// 다음 목표 타일을 산출 후 충돌 맵에 차단 여부 질의.
			// 차단/맵 경계 밖이면 위치 갱신 없이 이동 취소.
			// (bFacingLeft는 위에서 이미 갱신됐으므로 좌우 입력 시 시선 전환은 유지된다 — B2 비대칭)
			const FVector2D NextTile = TilePos + Dir;
			const int32 NextTileX = static_cast<int32>(NextTile.X);
			const int32 NextTileY = static_cast<int32>(NextTile.Y);

			const std::shared_ptr<UCollisionMap>& CollisionMap = World ? World->GetCollisionMap() : nullptr;
			const bool bBlocked = (CollisionMap && CollisionMap->IsBlocked(NextTileX, NextTileY));

			if (!bBlocked)
			{
				TilePos = NextTile;
				TargetPos = TilePos * static_cast<float>(TileSize);
				bIsMoving = true;
			}
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
}