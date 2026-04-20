#pragma once

#include "ACharacterActor.h"

/** 서버 권위 몬스터 액터 (클라이언트 측). */
class AMonsterActor : public ACharacterActor
{
public:
	AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY);
	virtual ~AMonsterActor() = default;

	uint64 GetMonsterID() const { return MonsterID; }

	/** S_MONSTER_SPAWN 수신 시 호출 — 즉시 해당 타일로 워프. */
	void OnServerSpawn(int32 InTileX, int32 InTileY);

	/** S_MONSTER_MOVE 수신 시 호출 — 목표 타일로 보간 이동 시작. */
	void OnServerMove(int32 InTileX, int32 InTileY);

protected:
	virtual float GetAttackDuration() const override { return AttackDuration; }

private:
	uint64 MonsterID = 0;

	// ---------------------------------------------------------------
	// 스프라이트 시트 레이아웃 (Mini Golem Sprite Sheet, 32×32 프레임)
	// ---------------------------------------------------------------

	static constexpr FSpriteClipInfo IdleClip   = { 0, 4,  6.f };
	static constexpr FSpriteClipInfo WalkClip   = { 1, 6, 10.f };
	static constexpr FSpriteClipInfo AttackClip = { 2, 7, 12.f };

	static constexpr float AttackDuration = static_cast<float>(AttackClip.Frames) / AttackClip.Fps;

	static constexpr float MonsterMoveSpeed = 100.f;
};