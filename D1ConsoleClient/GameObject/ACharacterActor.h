#pragma once

#include "AnimActor.h"
#include "Core/FVector2D.h"

/** 스프라이트 시트 한 클립(행)의 레이아웃 정보. */
struct FSpriteClipInfo
{
	int32 Row;
	int32 Frames;
	float Fps;
};

/** 월드에 실존하는 이동/전투 가능한 캐릭터 공용 베이스. */
class ACharacterActor : public AnimActor
{
public:
	/** 캐릭터 공용 애니메이션 클립 ID. 실제 Row/FrameCount/Fps 는 파생이 설정. */
	enum class EAnimClip : int32 { Idle = 0, Walk = 1, Attack = 2 };

public:
	ACharacterActor() = default;
	virtual ~ACharacterActor() = default;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(HDC BackDC) override;

	virtual int32 GetTileX() const override { return static_cast<int32>(TilePos.X); }
	virtual int32 GetTileY() const override { return static_cast<int32>(TilePos.Y); }

	int32 GetHP() const { return HP; }
	int32 GetMaxHP() const { return MaxHP; }

	/** 서버 피격 패킷 수신 시 호출. */
	virtual void OnServerDamaged(int32 InHP, int32 InMaxHP);

	/** 서버 사망 패킷 수신 시 호출. 기본 구현은 HP=0 세팅. */
	virtual void OnServerDied();

	/** 서버 공격 액션 패킷 수신 시 호출. 기본 구현은 BeginAttack() 으로 애니메이션만 재생. */
	virtual void OnServerAttack();

protected:
	/** 파생 클래스는 공격 클립 1사이클 재생 시간(초)을 반환한다. bIsAttacking 해제 기준. */
	virtual float GetAttackDuration() const = 0;

	/** 공격 상태 진입 (애니메이션 재생 시작). */
	void BeginAttack();

	/** 이동 타겟 설정 + 픽셀 보간 시작. */
	void BeginMoveTo(int32 NextTileX, int32 NextTileY);

	/** 지정 타일로 즉시 워프 (보간 없음). */
	void WarpTo(int32 TileX, int32 TileY);

	/** 상태 우선순위 Attack > Walk > Idle 에 따라 Sprite 클립 전환. */
	void UpdateAnimationState();

	/** 이동 타일 보간. 목표 도달 시 bIsMoving=false. */
	void UpdateMovement(float DeltaTime);

	// ---------------------------------------------------------------
	// 상태 (파생 공용)
	// ---------------------------------------------------------------

	/** 논리 타일 좌표. 보간 중이면 이미 다음 타일을 가리킬 수 있음. */
	FVector2D TilePos;

	/** 보간 목표 픽셀 좌표 (TilePos * TileSize). */
	FVector2D TargetPos;

	/** 타일 간 이동 보간 중 여부. */
	bool bIsMoving = false;

	/** 좌향 이동 시 true — Sprite 수평 반전. */
	bool bFacingLeft = false;

	/** 공격 애니메이션 재생 중. */
	bool bIsAttacking = false;

	/** 공격 시작 이후 누적 시간(초). GetAttackDuration() 도달 시 bIsAttacking=false. */
	float AttackTimer = 0.f;

	/** 서버 권위 HP / MaxHP. */
	int32 HP = 0;
	int32 MaxHP = 0;

	/** 픽셀/초 이동 속도. 파생 클래스 생성자에서 종별 값을 덮어쓴다. */
	float MoveSpeed = 200.f;

	// ---------------------------------------------------------------
	// 공용 튜닝값
	// ---------------------------------------------------------------

	/** 타일 한 변 픽셀 크기. */
	static constexpr int32 TileSize = 32;

	/** 출력 스프라이트 한 변 픽셀 크기 (시트 프레임보다 2배 확대). */
	static constexpr int32 RenderSize = 64;
};