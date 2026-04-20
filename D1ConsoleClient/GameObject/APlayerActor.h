#pragma once

#include "ACharacterActor.h"
#include "Protocol.pb.h"

#include <memory>
#include <vector>

class UHealthBarWidget;


/** 예측 이동 1건 스냅샷 — 서버 확정/거절 시 매칭·clip 기준으로 사용. */
struct FMoveSnapshot
{
    uint64 ClientSeq = 0;
    int32 TileX = 0;
    int32 TileY = 0;
    Protocol::Direction Dir = Protocol::DIR_DOWN;
};

/**
 * 캐릭터 스프라이트 시트 설정 번들. TextureName + 3종 애니메이션 클립(Idle/Walk/Attack) 을 한 묶음으로 다룬다.
 * 각 파생 PlayerActor 가 자신의 static constexpr Config 를 선언하고 APlayerActor 베이스 생성자에 주입하여
 * 클립 레이아웃이 시트마다 달라도 (프레임 수 차이 등) 분기 코드 없이 흡수된다.
 */
struct FCharacterSpriteConfig
{
	/** ResourceManager 에 등록된 텍스처 이름 (예: L"PlayerSprite", L"PlayerFemaleSprite"). */
	const wchar_t* TextureName;
	FSpriteClipInfo IdleClip;
	FSpriteClipInfo WalkClip;
	FSpriteClipInfo AttackClip;
};

/** 플레이어가 조종하는 액터. */
class APlayerActor : public ACharacterActor
{
public:
	/** 기본 Adventurer 스프라이트 Config. 파생 클래스를 만들지 않고 스폰할 때 폴백으로 쓰인다. */
	static constexpr FCharacterSpriteConfig DefaultConfig = {
		L"PlayerSprite",
		{ 0, 13, 8.f },
		{ 1, 8, 25.f },
		{ 3, 10, 15.f }
	};

	/**
	 * 파생 클래스가 자신의 static Config 를 전달해 스프라이트/클립을 완전히 맞춤화할 수 있다.
	 * 생성자 virtual 호출 불가 이슈를 피하기 위해 런타임 값(참조) 로 주입한다.
	 */
	APlayerActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY,	const FCharacterSpriteConfig& InConfig = DefaultConfig);

	virtual ~APlayerActor() override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(HDC BackDC) override;

	uint64 GetPlayerID() const { return PlayerID; }

	/** 이 액터가 로컬 클라이언트의 조작 대상인지 반환한다. */
	bool IsLocallyControlled() const;

	/** 서버가 이동을 승인했을 때(S_MOVE 수신) 호출한다. */
	void OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir, uint64 ClientSeq);

	/** 서버가 이동을 거절했을 때(S_MOVE_REJECT 수신) 호출한다. RejectedSeq 는 거절된 C_MOVE 의 client_seq. */
	void OnServerMoveRejected(uint64 RejectedSeq, uint64 LastAcceptedSeq, int32 ServerTileX, int32 ServerTileY);

	/** 서버가 S_ENTER_GAME 으로 내려 준 TileMoveSpeed 를 반영한다. 로컬 입력 쿨다운이 서버와 동일해지도록. */
	void SetTileMoveSpeed(float InSpeed) { TileMoveSpeed = InSpeed; }

	/** Portal 전이 등 외부 이유로 pending snapshot 을 전량 폐기해야 할 때 호출한다. */
	void ClearSnapshotQueue();

	/** 체력바 갱신 + 베이스의 Hit 이펙트 스폰 훅 호출. */
	virtual void OnServerDamaged(int32 InHP, int32 InMaxHP) override;

protected:
	/** 공격 1사이클 재생 시간(초). 생성자에서 Config.AttackClip 기반으로 계산되어 여기 저장된다. */
	float AttackDuration = 0.f;

	virtual float GetAttackDuration() const override { return AttackDuration; }

private:
	void ProcessInput();
	void HandleAttackInput();
	void HandleMoveInput();

	/** reject/mismatch 수신 시 SnapshotQueue 의 Dir 들을 PendingReplayDirs 로 옮기고 SnapshotQueue 는 비운다. C_MOVE 재송신은 하지 않는다. */
	void StageReplayFromSnapshots();

	/** PendingReplayDirs front 1 건을 소비해 한 타일 만큼 BeginMoveTo 진행. Tick 이 매 프레임 bIsMoving==false 일 때 호출한다. CanMoveTo 실패 시 해당 Dir drop. */
	void AdvanceReplayStep();

	/** 서버가 부여한 플레이어 고유 ID. */
	uint64 PlayerID = 0;

	/** C_MOVE 에 실어 보낼 단조 증가 시퀀스. 0 은 "아직 이동하지 않음" 을 의미하도록 1 부터 시작. */
	uint64 NextClientSeq = 1;

	/** 예측 스냅 샷 저장 배열 */
	std::vector<FMoveSnapshot> SnapshotQueue;

	/** S_ENTER_GAME 으로 서버가 내려 준 타일/초 이동 속도. 로컬 쿨다운 = 1000 / TileMoveSpeed (ms). */
	float TileMoveSpeed = DefaultTileMoveSpeed;

	/** HandleMoveInput 이 마지막으로 서버에 입력을 보낸 GetTickCount64 시각(ms). 0 은 "아직 보낸 적 없음". */
	uint64 LastMoveTimeMs = 0;

	/** 연속 reject 횟수. replay 성공(OnServerMoveAccepted 정상 pop) 시 0 으로 리셋. MaxReconcileRetries 초과 시 warp-only 로 fallback. */
	int32 ReconcileRetryCount = 0;

	/** reject/mismatch 수신 시 StageReplayFromSnapshots 로 채워지는 Dir 대기열. Tick 이 매 프레임 한 건씩 소비. */
	std::vector<Protocol::Direction> PendingReplayDirs;

	/** replay 진행 중 플래그. 진입 시 MoveSpeed 를 ReplayMoveSpeedMultiplier 배로 증폭, 종료 시 원복. 엣지 감지용이라 한 번씩만 토글된다. */
	bool bInReplayMode = false;

	/** 플레이어 머리 위에 렌더되는 체력바. APlayerActor 가 소유(World Actor 아님). */
	std::unique_ptr<UHealthBarWidget> HealthBar;

	/** 픽셀/초 — 생성자에서 ACharacterActor::MoveSpeed 에 덮어쓴다. */
	static constexpr float PlayerMoveSpeed = 260.f;

	/** 서버 Level::DefaultPlayerTileMoveSpeed 와 맞춘 폴백 값. S_ENTER_GAME 수신 전 스폰 직후 잠깐 사용. */
	static constexpr float DefaultTileMoveSpeed = 4.0f;

	/** 한 번의 reject 이벤트 체인에서 허용하는 replay 재시도 최대치. 초과 시 warp-only fallback 으로 무한 루프 차단. */
	static constexpr int32 MaxReconcileRetries = 3;

	/** replay 중 MoveSpeed 증폭 배율. 클수록 빠르게 복구하지만 너무 크면 스르륵 감각이 사라진다. */
	static constexpr float ReplayMoveSpeedMultiplier = 4.0f;
};