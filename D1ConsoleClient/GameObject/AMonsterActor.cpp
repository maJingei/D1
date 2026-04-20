#include "AMonsterActor.h"

#include "Render/ResourceManager.h"
#include "Render/Sprite.h"

AMonsterActor::AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY)
	: MonsterID(InMonsterID)
{
	// 초기 타일 좌표로 즉시 워프.
	WarpTo(InTileX, InTileY);

	// 이동 속도 주입.
	MoveSpeed = MonsterMoveSpeed;

	// 스프라이트 초기화 — Mini Golem 시트.
	ActorSprite = std::make_shared<Sprite>();
	auto Texture = ResourceManager::Get().GetTexture(L"MiniGolemSprite");
	ActorSprite->Init(Texture, TileSize);
	ActorSprite->SetRenderSize(RenderSize);

	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { IdleClip.Row,   IdleClip.Frames,   IdleClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { WalkClip.Row,   WalkClip.Frames,   WalkClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { AttackClip.Row, AttackClip.Frames, AttackClip.Fps });
	ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));
}

void AMonsterActor::OnServerSpawn(int32 InTileX, int32 InTileY)
{
	WarpTo(InTileX, InTileY);
}

void AMonsterActor::OnServerMove(int32 InTileX, int32 InTileY)
{
	// 좌/우 이동이면 수평 반전 플래그 갱신.
	if (InTileX < static_cast<int32>(TilePos.X))
		bFacingLeft = true;
	else if (InTileX > static_cast<int32>(TilePos.X))
		bFacingLeft = false;

	BeginMoveTo(InTileX, InTileY);
}
