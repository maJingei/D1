#include "AMonsterActor.h"

#include "Render/ResourceManager.h"
#include "Render/Sprite.h"

AMonsterActor::AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY, const FMonsterSpriteConfig& InConfig)
	: MonsterID(InMonsterID)
{
	// 초기 타일 좌표로 즉시 워프.
	WarpTo(InTileX, InTileY);

	// 이동 속도 주입.
	MoveSpeed = MonsterMoveSpeed;

	// 공격 1사이클 길이 캐싱 — 클립 프레임 수가 종류별로 다르므로 ctor 시점에 1회 계산.
	AttackDuration = static_cast<float>(InConfig.AttackClip.Frames) / InConfig.AttackClip.Fps;

	// 스프라이트 초기화 — Config 로 받은 텍스처/셀크기/출력크기/클립 레이아웃을 그대로 적용.
	// FrameSize 는 시트별로 다름(32/128/256). TileSize/RenderSize 베이스 상수가 아닌 Config 값을 따른다.
	ActorSprite = std::make_shared<Sprite>();
	auto Texture = ResourceManager::Get().GetTexture(InConfig.TextureName);
	ActorSprite->Init(Texture, InConfig.FrameSize);
	ActorSprite->SetRenderSize(InConfig.RenderSize);

	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { InConfig.IdleClip.Row,   InConfig.IdleClip.Frames,   InConfig.IdleClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { InConfig.WalkClip.Row,   InConfig.WalkClip.Frames,   InConfig.WalkClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { InConfig.AttackClip.Row, InConfig.AttackClip.Frames, InConfig.AttackClip.Fps });
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
