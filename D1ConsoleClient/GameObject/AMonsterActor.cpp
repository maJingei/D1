#include "AMonsterActor.h"

#include "Render/ResourceManager.h"

AMonsterActor::AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY)
{
	MonsterID = InMonsterID;
	TilePos = { InTileX, InTileY };
	TargetPos = { static_cast<float>(InTileX * TileSize), static_cast<float>(InTileY * TileSize) };
	X = TargetPos.X;
	Y = TargetPos.Y;

	ActorSprite = std::make_shared<Sprite>();
	auto Texture = ResourceManager::Get().GetTexture(L"MiniGolemSprite");
	ActorSprite->Init(Texture, GolemFrameSize);
	ActorSprite->SetRenderSize(RenderSize);
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { IdleClip.Row,   IdleClip.Frames,   IdleClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { WalkClip.Row,   WalkClip.Frames,   WalkClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { AttackClip.Row, AttackClip.Frames, AttackClip.Fps });
	ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));
}

void AMonsterActor::OnServerSpawn(int32 InTileX, int32 InTileY)
{
	TilePos = { InTileX, InTileY };
	TargetPos = { static_cast<float>(InTileX * TileSize), static_cast<float>(InTileY * TileSize) };
	X = TargetPos.X;
	Y = TargetPos.Y;
}

void AMonsterActor::OnServerMove(int32 InTileX, int32 InTileY)
{
	// 방향 반전 판단 — 새 타일이 현재보다 좌측이면 flip
	if (InTileX < TilePos.X) bFacingLeft = true;
	else if (InTileX > TilePos.X) bFacingLeft = false;

	TilePos = { InTileX, InTileY };
	TargetPos = { static_cast<float>(InTileX * TileSize), static_cast<float>(InTileY * TileSize) };
	bIsMoving = true;
}

void AMonsterActor::OnServerAttack()
{
	bIsAttacking = true;
	AttackTimer = 0.f;
}

void AMonsterActor::Tick(float DeltaTime)
{
	// 공격 타이머 진행
	if (bIsAttacking)
	{
		AttackTimer += DeltaTime;
		if (AttackTimer >= AttackDuration)
		{
			bIsAttacking = false;
			AttackTimer = 0.f;
		}
	}

	UpdateMovement(DeltaTime);

	// 상태 우선순위: Attack > Walk > Idle
	EAnimClip CurrentClip = EAnimClip::Idle;
	if (bIsAttacking) CurrentClip = EAnimClip::Attack;
	else if (bIsMoving) CurrentClip = EAnimClip::Walk;

	if (ActorSprite)
		ActorSprite->SetClipId(static_cast<int32>(CurrentClip));

	AnimActor::Tick(DeltaTime);
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
	constexpr int32 OffsetX = (TileSize - RenderSize) / 2;
	constexpr int32 OffsetY = TileSize - RenderSize;
	if (ActorSprite)
		ActorSprite->Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, bFacingLeft);
}
