#pragma once

#include "AnimActor.h"
#include "Core/FVector2D.h"
#include "Protocol.pb.h"

// 스프라이트 시트 레이아웃 (Adventurer Sprite Sheet v1.6, 32×32 프레임)
struct FSpriteClipInfo
{
    int32 Row;
    int32 Frames;
    float Fps;
};

/**
 * 플레이어가 조종하는 액터.
 * WASD 입력에 따라 타일 단위로 이동하며, 렌더링 위치는 부드럽게 보간된다.
 * Sprite를 통해 Idle/Walk 애니메이션을 재생하고, 좌향 이동 시 수평 반전한다.
 *
 * Sprite 소유/Update 는 AnimActor가 담당하고, 이 클래스는 입력/이동/상태만 관리한다.
 *
 * 이동 모델은 서버 권위적:
 *  - 로컬 플레이어는 C_MOVE 송신 후 bIsPending=true로 잠그고, S_MOVE 수신 시 비로소 TilePos/TargetPos 갱신 + bIsMoving=true.
 *  - 타 플레이어도 S_MOVE 수신 시 동일한 경로로 갱신된다 (bIsPending 은 본인 전용 플래그).
 */
class APlayerActor : public AnimActor
{
public:
	/**
	 * 스폰 타일 좌표를 받아 PlayerActor를 생성한다. 서버가 S_EnterGame/S_Spawn으로 전달한 좌표를 그대로 사용하도록 설계되어 있다.
	 *
	 * @param InPlayerID  서버가 부여한 플레이어 고유 ID
	 * @param SpawnTileX  스폰 타일 X
	 * @param SpawnTileY  스폰 타일 Y
	 */
	APlayerActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY);

	void Tick(float DeltaTime) override;
	void Render(HDC BackDC) override;

	/** 현재 논리 타일 좌표(정수). 이동 보간 중에도 TilePos는 다음 타일로 이미 갱신돼 있음에 주의. */
	int32 GetTileX() const override { return static_cast<int32>(TilePos.X); }
	int32 GetTileY() const override { return static_cast<int32>(TilePos.Y); }

	uint64 GetPlayerID() const { return PlayerID; }

	/**
	 * 이 액터가 로컬 클라이언트의 조작 대상인지 반환한다.
	 */
	bool IsLocallyControlled() const;

	/**
	 * 서버가 이동을 승인했을 때(S_MOVE 수신) 호출한다. 로컬/타인 공통 경로.
	 * TilePos/TargetPos를 갱신하고 bIsMoving=true 로 픽셀 보간을 시작시킨다. 로컬이면 bIsPending도 해제.
	 *
	 * @param NextTileX  서버가 확정한 목적 타일 X
	 * @param NextTileY  서버가 확정한 목적 타일 Y
	 * @param Dir        서버가 확정한 이동 방향 (좌향이면 Sprite 수평 반전)
	 */
	void OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir);

	/**
	 * 서버가 이동을 거절했을 때(S_MOVE_REJECT 수신) 호출한다. 로컬 플레이어에게만 유효.
	 * bIsPending 을 해제해 입력을 다시 받는다. 현재 위치는 서버가 준 좌표로 강제 동기화한다.
	 */
	void OnServerMoveRejected(int32 CurTileX, int32 CurTileY);

	/** S_PLAYER_DAMAGED 수신 시 호출 — HP 를 서버 권위 값으로 갱신한다. */
	void OnServerDamaged(int32 InHP, int32 InMaxHP) { HP = InHP; MaxHP = InMaxHP; }

	/** S_PLAYER_DIED 수신 시 호출 — 사망 상태를 기록한다. MVP 에서는 로그만 남긴다. */
	void OnServerDied() { HP = 0; }

	int32 GetHP() const { return HP; }
	int32 GetMaxHP() const { return MaxHP; }

private:
	/** 이동/공격 중이 아닐 때 입력을 읽어 다음 목표 타일 또는 공격을 시작한다. */
	void ProcessInput();

	/** 공격 입력(Space)을 처리한다. 공격 상태 진입 및 타이머 리셋. */
	void HandleAttackInput();

	/** 이동 입력(WASD)을 처리한다. 로컬 사전 필터를 통과하면 C_MOVE 송신 + bIsPending 잠금. 실제 이동은 S_MOVE 수신 시 시작. */
	void HandleMoveInput();

	/** 렌더링 위치를 목표 픽셀 위치로 보간한다. */
	void UpdateMovement(float DeltaTime);

	// ---------------------------------------------------------------
	// 플레이어 식별
	// 같은 월드에 여러 APlayerActor가 공존하므로 서버가 부여한 ID로 구분한다.
	// 로컬 여부는 Game::MyPlayerID 와의 비교로 런타임에 도출한다 (IsLocallyControlled).
	// ---------------------------------------------------------------

	/** 서버가 부여한 플레이어 고유 ID */
	uint64 PlayerID = 0;

	/** 서버 권위 HP. S_PLAYER_DAMAGED 수신 시 갱신된다. */
	int32 HP = 100;
	int32 MaxHP = 100;

	// ---------------------------------------------------------------
	// 이동
	// ---------------------------------------------------------------

	/** 현재 목표 타일 좌표 */
	FVector2D TilePos;

	/** 픽셀 단위 목표 위치 (TilePos * TileSize) */
	FVector2D TargetPos;

	/** 타일 간 이동이 진행 중인지 */
	bool bIsMoving = false;

	/** C_MOVE 송신 후 S_MOVE/S_MOVE_REJECT 수신 전 상태. 로컬 플레이어 전용. true 인 동안 추가 이동 입력 차단. */
	bool bIsPending = false;

	static constexpr int32 TileSize = 32;
	/** 스프라이트 화면 출력 크기 (TileSize의 2배 → 32×32 시트를 64×64로 확대 출력). */
	static constexpr int32 RenderSize = 64;
	static constexpr float MoveSpeed = 260.f; // 픽셀/초

	// ---------------------------------------------------------------
	// 애니메이션
	// ---------------------------------------------------------------

	enum class EAnimClip { Idle = 0, Walk = 1, Attack = 2 };

	/** A키(좌향) 이동 시 true — Sprite::Render에 수평 반전 전달 */
	bool bFacingLeft = false;

	// ---------------------------------------------------------------
	// 공격
	// ---------------------------------------------------------------

	/** 공격 애니메이션 재생 중인지. true인 동안 모든 입력(이동/추가 공격) 차단. */
	bool bIsAttacking = false;

	/** 공격 시작 이후 누적 시간(초). AttackDuration 도달 시 Idle로 복귀. */
	float AttackTimer = 0.f;

	static constexpr FSpriteClipInfo IdleClip   = { 0, 13, 8.f };
	static constexpr FSpriteClipInfo WalkClip   = { 1,  8, 25.f };
	static constexpr FSpriteClipInfo AttackClip = { 3, 10, 15.f };

	/** 공격 1사이클 재생 시간(초). Sprite가 자동 루프하므로 이 시간만큼 지나면 강제로 Idle 복귀시킨다. */
	static constexpr float AttackDuration = static_cast<float>(AttackClip.Frames) / AttackClip.Fps;
};
