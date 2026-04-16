#pragma once

#include "AnimActor.h"
#include "APlayerActor.h"
#include "Core/FVector2D.h"

#include <memory>

/**
 * 서버 권위 몬스터 액터 (클라이언트 측).
 *
 * 로컬 길찾기/공격 판정 로직 없음. 서버가 broadcast 하는 패킷만 소비한다.
 * OnServerSpawn / OnServerMove / OnServerAttack 으로 상태가 갱신된다.
 */
class AMonsterActor : public AnimActor
{
public:
	AMonsterActor(uint64 InMonsterID, int32 InTileX, int32 InTileY);

	void Tick(float DeltaTime) override;
	void Render(HDC BackDC) override;

	uint64 GetMonsterID() const { return MonsterID; }
	int32 GetTileX() const override { return TilePos.X; }
	int32 GetTileY() const override { return TilePos.Y; }

	/** S_MONSTER_SPAWN 수신 시 호출 — 이미 생성자에서 처리되므로 추가 초기화용. */
	void OnServerSpawn(int32 InTileX, int32 InTileY);

	/** S_MONSTER_MOVE 수신 시 호출 — 목표 타일로 보간 이동을 시작한다. */
	void OnServerMove(int32 InTileX, int32 InTileY);

	/** S_MONSTER_ATTACK 수신 시 호출 — 공격 애니메이션만 트리거한다. */
	void OnServerAttack();

private:
	/** 타일 간 이동 보간 진행. 목표 도달 시 bIsMoving = false. */
	void UpdateMovement(float DeltaTime);

	// ---------------------------------------------------------------
	// 식별
	// ---------------------------------------------------------------

	uint64 MonsterID = 0;

	// ---------------------------------------------------------------
	// 위치/이동
	// ---------------------------------------------------------------

	struct FTileCoord { int32 X = 0; int32 Y = 0; };

	/** 논리 타일 좌표. 이동 보간 중에도 다음 도착 예정 타일을 가리킨다. */
	FTileCoord TilePos;

	/** 보간 목표 픽셀 좌표 */
	FVector2D TargetPos;

	/** 타일 간 이동 보간 진행 중 여부 */
	bool bIsMoving = false;

	// ---------------------------------------------------------------
	// 애니메이션
	// ---------------------------------------------------------------

	enum class EAnimClip { Idle = 0, Walk = 1, Attack = 2 };

	/** 좌향 이동 시 수평 반전 */
	bool bFacingLeft = false;

	/** 공격 애니메이션 재생 중 여부 */
	bool bIsAttacking = false;

	/** 공격 시작 이후 누적 시간(초) */
	float AttackTimer = 0.f;

	// ---------------------------------------------------------------
	// 튜닝값 (매직 넘버 금지)
	// ---------------------------------------------------------------

	static constexpr int32 TileSize = 32;
	static constexpr int32 RenderSize = 64;

	static constexpr float MoveSpeed = 100;
	static constexpr FSpriteClipInfo IdleClip   = { 0, 4, 6.f };
	static constexpr FSpriteClipInfo WalkClip   = { 1, 6, 10.f };
	static constexpr FSpriteClipInfo AttackClip = { 2, 7, 12.f };
	static constexpr int32 GolemFrameSize = 32;
	static constexpr float AttackDuration = static_cast<float>(AttackClip.Frames) / AttackClip.Fps;
};
