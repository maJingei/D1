#include "ACharacterActor.h"

#include "Effect/AHitEffectActor.h"
#include "World/UWorld.h"
#include "Render/Sprite.h"

void ACharacterActor::Tick(float DeltaTime)
{
	// 1. 공격 타이머 진행. Sprite 는 자동 루프하므로 파생별 AttackDuration 경과 시 해제.
	if (bIsAttacking)
	{
		AttackTimer += DeltaTime;
		if (AttackTimer >= GetAttackDuration())
		{
			bIsAttacking = false;
			AttackTimer = 0.f;
		}
	}

	// 2. 타일 이동 보간. 공격 중이면 bIsMoving 이 false 이므로 no-op.
	UpdateMovement(DeltaTime);

	// 3. Sprite 클립 상태 갱신 (Attack > Walk > Idle)
	UpdateAnimationState();

	// 4. AnimActor::Tick 이 ActorSprite->Update(DeltaTime) 호출
	AnimActor::Tick(DeltaTime);
}

void ACharacterActor::Render(HDC BackDC)
{
	// 확대 스프라이트를 타일 중앙(가로) + 발끝을 타일 하단에 정렬.
	// OffsetX 는 음수: 가로 중앙을 맞추기 위해 좌측으로 (RenderSize-TileSize)/2 만큼 당김.
	// OffsetY 는 음수: 발끝을 타일 하단에 맞추기 위해 위로 (RenderSize-TileSize) 만큼 끌어올림.
	constexpr int32 OffsetX = (TileSize - RenderSize) / 2;
	constexpr int32 OffsetY = TileSize - RenderSize;
	if (ActorSprite)
		ActorSprite->Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, bFacingLeft);
}

void ACharacterActor::OnServerDamaged(int32 InHP, int32 InMaxHP)
{
	HP = InHP;
	MaxHP = InMaxHP;

	// Hit 이펙트 스폰 — 피격자 중앙 고정 위치.
	if (World == nullptr)
		return;
	const float CenterX = X + static_cast<float>(TileSize) * 0.5f;
	const float CenterY = Y + static_cast<float>(TileSize) * 0.5f;
	World->SpawnActor<AHitEffectActor>(CenterX, CenterY);
}

void ACharacterActor::OnServerDied()
{
	HP = 0;
}

void ACharacterActor::BeginAttack()
{
	bIsAttacking = true;
	AttackTimer = 0.f;
}

void ACharacterActor::BeginMoveTo(int32 NextTileX, int32 NextTileY)
{
	TilePos = { static_cast<float>(NextTileX), static_cast<float>(NextTileY) };
	TargetPos = TilePos * static_cast<float>(TileSize);
	bIsMoving = true;
}

void ACharacterActor::WarpTo(int32 TileX, int32 TileY)
{
	TilePos = { static_cast<float>(TileX), static_cast<float>(TileY) };
	TargetPos = TilePos * static_cast<float>(TileSize);
	X = TargetPos.X;
	Y = TargetPos.Y;
	bIsMoving = false;
}

void ACharacterActor::UpdateAnimationState()
{
	if (ActorSprite == nullptr)
		return;
	EAnimClip CurrentClip = EAnimClip::Idle;
	if (bIsAttacking)
		CurrentClip = EAnimClip::Attack;
	else if (bIsMoving)
		CurrentClip = EAnimClip::Walk;
	ActorSprite->SetClipId(static_cast<int32>(CurrentClip));
}

void ACharacterActor::UpdateMovement(float DeltaTime)
{
	if (!bIsMoving)
		return;

	FVector2D RenderPos = { X, Y };
	FVector2D Delta = TargetPos - RenderPos;
	float DistSq = Delta.LengthSquared();
	float Step = MoveSpeed * DeltaTime;

	if (Step * Step >= DistSq)
	{
		// 목표 도달: 위치 확정 및 이동 종료
		X = TargetPos.X;
		Y = TargetPos.Y;
		bIsMoving = false;
	}
	else
	{
		// 목표 방향으로 Step 만큼 전진
		FVector2D Dir = Delta.Normalized();
		X += Dir.X * Step;
		Y += Dir.Y * Step;
	}
}