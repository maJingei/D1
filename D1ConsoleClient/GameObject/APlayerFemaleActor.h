#pragma once

#include "APlayerActor.h"

/**
 * 여자 어드벤처러 스프라이트를 쓰는 PlayerActor 파생 클래스.
 * 스탯/애니메이션 로직은 APlayerActor 와 완전히 동일하며, 자기 Config (스프라이트 시트 + 클립 프레임 수)만 갖는다.
 * 시트 레이아웃이 기본 Adventurer 와 달라 Idle=6, Walk=8, Attack=9 프레임으로 각각 조정되어 있다.
 */
class APlayerFemaleActor : public APlayerActor
{
public:
	/** Female 전용 스프라이트 시트 Config — Row 는 기본과 동일, 프레임 수(Frames)만 시트 실제에 맞춰 조정. */
	static constexpr FCharacterSpriteConfig Config = {
		L"PlayerFemaleSprite",
		{ 0, 6, 8.f },
		{ 1, 8, 25.f },
		{ 2, 9, 15.f }
	};

	APlayerFemaleActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY)
		: APlayerActor(InPlayerID, SpawnTileX, SpawnTileY, Config) {}
};