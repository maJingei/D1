#pragma once

#include "AMonsterActor.h"

/**
 * Mini Golem(슬라임) 스프라이트를 쓰는 MonsterActor 파생 클래스.
 * 시트 레이아웃: Idle 0행 4프레임 / Walk 1행 6프레임 / Attack 2행 7프레임.
 * 게임플레이 로직은 AMonsterActor 와 동일하며 자기 Config 만 갖는다.
 */
class ASlimeActor : public AMonsterActor
{
public:
	// Mini Golem 시트 256×160 (8열×5행) → 셀 32×32. 화면 출력은 64 (2× 명도).
	static constexpr FMonsterSpriteConfig Config = {
		L"MiniGolemSprite",
		32, 64,
		{ 0, 4,  6.f },
		{ 1, 6, 10.f },
		{ 2, 7, 12.f }
	};

	ASlimeActor(uint64 InMonsterID, int32 InTileX, int32 InTileY)
		: AMonsterActor(InMonsterID, InTileX, InTileY, Config) {}
};
