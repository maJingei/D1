#pragma once

#include "APlayerActor.h"

/**
 * 사무라이(FREE_Samurai 2D Pixel Art) 스프라이트를 쓰는 PlayerActor 파생 클래스.
 * 스탯/행동 로직은 APlayerActor 와 완전히 동일하며, 자기 Config(Mirror 모드 + 상태별 시트 3장 + 클립 메타데이터)만 갖는다.
 *
 * Mirror 모드 특성: 한 시트가 오른쪽 향함 1방향만 그려져 있어 좌향은 bFacingLeft 로 GDI 미러링 처리.
 * 현재 운영 정책상 봇 전용으로 사용된다(사람 클라는 CT_DEFAULT, 봇은 CT_SAMURAI).
 */
class APlayerSamuraiActor : public APlayerActor
{
public:
	/**
	 * 사무라이 전용 Config — Mirror 모드 + 상태별 1장 시트 + 96×96 프레임을 192×192 로 2배 출력.
	 * 사이클 시간(자연스러운 기본값): Idle 1.25초, Walk 0.667초, Attack 0.5초.
	 */
	static constexpr FCharacterSpriteConfig Config = {
		nullptr,                                                     // TextureName (Mirror 모드에서는 미사용)
		{},                                                          // IdleClip   (legacy)
		{},                                                          // WalkClip   (legacy)
		{},                                                          // AttackClip (legacy)
		FCharacterSpriteConfig::EMode::Mirror,                       // Mode
		{},                                                          // IdleDirClip   (directional 미사용)
		{},                                                          // WalkDirClip   (directional 미사용)
		{},                                                          // AttackDirClip (directional 미사용)
		{ L"PlayerSamuraiIdle",   10,  8.0f },                       // IdleMirrorClip   (10프레임 × 8fps = 1.25초/loop)
		{ L"PlayerSamuraiWalk",   16, 24.0f },                       // WalkMirrorClip   (16프레임 × 24fps = 0.667초/loop)
		{ L"PlayerSamuraiAttack",  7, 14.0f },                       // AttackMirrorClip (7프레임 × 14fps = 0.5초/loop)
		96, 96,                                                      // FrameW, FrameH (시트 잘라내기 영역)
		192, 192                                                     // RenderW, RenderH (2× 확대 출력, NearestNeighbor)
	};

	APlayerSamuraiActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY)
		: APlayerActor(InPlayerID, SpawnTileX, SpawnTileY, Config) {}
};