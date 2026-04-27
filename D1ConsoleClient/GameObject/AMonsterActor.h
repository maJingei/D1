#pragma once

#include "ACharacterActor.h"

/**
 * 몬스터 스프라이트 시트 설정 번들. TextureName + 셀 픽셀 크기 + 화면 출력 크기 + 3종 애니메이션 클립(Idle/Walk/Attack).
 * FrameSize 가 시트마다 다른 이유: Mini Golem 32 / Minotaur 128 / Orc 256 — 셀 크기를 종류별로 주입하지 않으면
 * 좌상단 32×32 영역만 잘라 그려져 캐릭터가 잘리거나 빈 영역만 표시되는 렌더 버그가 발생한다.
 */
struct FMonsterSpriteConfig
{
	/** ResourceManager 에 등록된 텍스처 이름 (예: L"MiniGolemSprite", L"MinotaurSprite"). */
	const wchar_t* TextureName;
	/** 시트 한 프레임의 픽셀 크기 (정사각). 시트 폭/(가장 긴 행 프레임 수) 로 측정해 결정. */
	int32 FrameSize;
	/** 화면 출력 크기. FrameSize 와 다르면 NearestNeighbor 로 확대/축소된다. */
	int32 RenderSize;
	FSpriteClipInfo IdleClip;
	FSpriteClipInfo WalkClip;
	FSpriteClipInfo AttackClip;
};

/** 서버 권위 몬스터 액터 (클라이언트 측). */
class AMonsterActor : public ACharacterActor
{
public:
	/** 기본(슬라임/Mini Golem) 스프라이트 Config. 파생을 만들지 않고 스폰할 때의 폴백. 셀 32×32, 화면 64×64. */
	static constexpr FMonsterSpriteConfig DefaultConfig = {
		L"MiniGolemSprite",
		32, 64,
		{ 0, 4,  6.f },
		{ 1, 6, 10.f },
		{ 2, 7, 12.f }
	};

	/**
	 * 파생 클래스가 자신의 static Config 를 전달해 스프라이트/클립을 완전히 맞춤화할 수 있다.
	 * 생성자 virtual 호출 불가 이슈를 피하기 위해 런타임 값(참조) 로 주입한다.
	 */
	AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY, const FMonsterSpriteConfig& InConfig = DefaultConfig);
	virtual ~AMonsterActor() override = default;

	uint64 GetMonsterID() const { return MonsterID; }

	/** S_MONSTER_SPAWN 수신 시 호출 — 즉시 해당 타일로 워프. */
	void OnServerSpawn(int32 InTileX, int32 InTileY);

	/** S_MONSTER_MOVE 수신 시 호출 — 목표 타일로 보간 이동 시작. */
	void OnServerMove(int32 InTileX, int32 InTileY);

protected:
	virtual float GetAttackDuration() const override { return AttackDuration; }

	/** 공격 클립 1사이클 재생 시간(초). 생성자에서 Config.AttackClip 기반으로 계산되어 저장된다. */
	float AttackDuration = 0.f;

private:
	uint64 MonsterID = 0;

	/** 픽셀/초 — 생성자에서 ACharacterActor::MoveSpeed 에 덮어쓴다. 종류와 무관하게 동일(MVP). */
	static constexpr float MonsterMoveSpeed = 100.f;
};
