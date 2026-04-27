#pragma once

#include "ACharacterActor.h"
#include "Protocol.pb.h"

#include <memory>
#include <string>
#include <vector>

class UHealthBarWidget;
class Texture;


/** 예측 이동 1건 스냅샷 — 서버 확정/거절 시 매칭·clip 기준으로 사용. */
struct FMoveSnapshot
{
    uint64 ClientSeq = 0;
    int32 TileX = 0;
    int32 TileY = 0;
    Protocol::Direction Dir = Protocol::DIR_DOWN;
};

/**
 * 한 상태(Idle/Walk/Attack)의 4방향 텍스처 + 클립 메타데이터 묶음.
 * Directional 모드(신규 PlayerSprite) 전용. 각 PNG 는 1행 × N프레임 레이아웃이라 Row 정보가 필요 없다.
 */
struct FDirectionalClip
{
	const wchar_t* DownTextureName;
	const wchar_t* LeftTextureName;
	const wchar_t* RightTextureName;
	const wchar_t* UpTextureName;
	int32 FrameCount;
	float Fps;
};

/**
 * 한 상태(Idle/Walk/Attack)의 단일 시트 + 클립 메타데이터 묶음.
 * Mirror 모드(Samurai 류) 전용 — 상태마다 PNG 1장(오른쪽 향함). 좌향은 bFacingLeft 로 GDI 미러.
 */
struct FMirrorClip
{
	const wchar_t* TextureName;
	int32 FrameCount;
	float Fps;
};

/**
 * 캐릭터 스프라이트 시트 설정 번들. 한 캐릭터의 텍스처/클립/프레임/렌더 크기를 한 묶음으로 다룬다.
 * 각 파생 PlayerActor 가 자신의 static constexpr Config 를 선언하고 APlayerActor 베이스 생성자에 주입하여
 * 클립 레이아웃이 시트마다 달라도 (프레임 수 차이 등) 분기 코드 없이 흡수된다.
 *
 * 세 가지 모드를 지원한다:
 *   - EMode::Legacy      — 단일 시트 row-기반 (Adventurer/Dwarf/Monster 류). TextureName + IdleClip/WalkClip/AttackClip 사용.
 *   - EMode::Directional — 상태×방향 4파일 멀티시트 (FREE_Adventurer 신규 에셋). IdleDirClip/WalkDirClip/AttackDirClip 사용 + FrameW/H/RenderW/H 명시.
 *   - EMode::Mirror      — 상태별 1장 시트(오른쪽 향함) + bFacingLeft 미러 (FREE_Samurai 류). IdleMirrorClip/WalkMirrorClip/AttackMirrorClip 사용 + FrameW/H/RenderW/H 명시.
 *
 * 기존 Dwarf 의 위치 기반 brace-init 호환을 위해 legacy 필드를 앞쪽에 두고, 신규 필드는 default-init 가능한 상태로 뒤에 둔다.
 */
struct FCharacterSpriteConfig
{
	enum class EMode : int32 { Legacy, Directional, Mirror };

	// -- Legacy fields (기존 Female/Dwarf 의 positional aggregate-init 와 호환되도록 선언 순서 보존)
	/** ResourceManager 에 등록된 텍스처 이름 (예: L"PlayerSprite", L"PlayerFemaleSprite"). Directional 모드에서는 nullptr. */
	const wchar_t* TextureName;
	FSpriteClipInfo IdleClip;
	FSpriteClipInfo WalkClip;
	FSpriteClipInfo AttackClip;

	// -- 모드/Directional/Mirror 신규 필드 (default 값을 가져 legacy positional init 에서 생략 가능)
	EMode Mode = EMode::Legacy;
	FDirectionalClip IdleDirClip = {};
	FDirectionalClip WalkDirClip = {};
	FDirectionalClip AttackDirClip = {};
	FMirrorClip IdleMirrorClip = {};
	FMirrorClip WalkMirrorClip = {};
	FMirrorClip AttackMirrorClip = {};

	/** 시트 한 프레임의 픽셀 크기. Legacy 모드는 32×32 정사각이 기본. */
	int32 FrameW = 32;
	int32 FrameH = 32;
	/** 화면 출력 픽셀 크기. Legacy 모드는 64×64(=2배 확대). */
	int32 RenderW = 64;
	int32 RenderH = 64;
};

/** 플레이어가 조종하는 액터. */
class APlayerActor : public ACharacterActor
{
public:
	/**
	 * 기본 PlayerSprite Config — FREE_Adventurer 4방향 멀티파일 에셋 사용 (Directional 모드).
	 * 파생 클래스를 만들지 않고 스폰할 때 폴백. 12장 텍스처가 ResourceManager 에 등록되어 있어야 한다.
	 * 사이클 시간은 기존 Adventurer 단일 시트와 매칭: Idle ≈ 1.6초, Walk ≈ 0.32초, Attack ≈ 0.667초.
	 */
	static constexpr FCharacterSpriteConfig DefaultConfig = {
		nullptr,                                                     // TextureName (Directional 모드에서는 미사용)
		{},                                                          // IdleClip   (legacy)
		{},                                                          // WalkClip   (legacy)
		{},                                                          // AttackClip (legacy)
		FCharacterSpriteConfig::EMode::Directional,                  // Mode
		{ L"PlayerIdle_Down",   L"PlayerIdle_Left",   L"PlayerIdle_Right",   L"PlayerIdle_Up",   8, 5.0f  },  // IdleDirClip
		{ L"PlayerWalk_Down",   L"PlayerWalk_Left",   L"PlayerWalk_Right",   L"PlayerWalk_Up",   8, 25.0f },  // WalkDirClip
		{ L"PlayerAttack_Down", L"PlayerAttack_Left", L"PlayerAttack_Right", L"PlayerAttack_Up", 8, 12.0f },  // AttackDirClip
		{},                                                          // IdleMirrorClip   (Mirror 모드 미사용)
		{},                                                          // WalkMirrorClip   (Mirror 모드 미사용)
		{},                                                          // AttackMirrorClip (Mirror 모드 미사용)
		96, 80,                                                      // FrameW, FrameH (768/8 × 80, 시트 잘라내기 영역)
		192, 160                                                     // RenderW, RenderH (2× 확대 출력, 비율 보존, NearestNeighbor)
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

	/** 서버가 S_ENTER_GAME(my/others) 또는 S_SPAWN 으로 내려 준 nameplate 텍스트를 적용. 머리 위 렌더에 사용. */
	void SetNameplateText(std::wstring InText) { NameplateText = std::move(InText); }
	const std::wstring& GetNameplateText() const { return NameplateText; }

	/** Portal 전이 등 외부 이유로 pending snapshot 을 전량 폐기해야 할 때 호출한다. */
	void ClearSnapshotQueue();

	/** 체력바 갱신 + 베이스의 Hit 이펙트 스폰 훅 호출. */
	virtual void OnServerDamaged(int32 InHP, int32 InMaxHP) override;

protected:
	/** 공격 1사이클 재생 시간(초). 생성자에서 Config.AttackClip 기반으로 계산되어 여기 저장된다. */
	float AttackDuration = 0.f;

	virtual float GetAttackDuration() const override { return AttackDuration; }

protected:
	/**
	 * 입력 기준 Walk 클립 유지 — bIsMoving 이 타일 경계에서 잠깐 false 가 되어도 WASD 가 눌려 있으면 Walk 를 유지해
	 * Sprite::SetClipId 의 frame 리셋(클립 변경 시 frame=0)이 일어나지 않도록 한다. 본인 캐릭터 한정 처리.
	 */
	virtual void UpdateAnimationState() override;

private:
	void ProcessInput();
	void HandleAttackInput();
	void HandleMoveInput();

	/** reject/mismatch 수신 시 SnapshotQueue 의 Dir 들을 PendingReplayDirs 로 옮기고 SnapshotQueue 는 비운다. C_MOVE 재송신은 하지 않는다. */
	void StageReplayFromSnapshots();

	/** PendingReplayDirs front 1 건을 소비해 한 타일 만큼 BeginMoveTo 진행. Tick 이 매 프레임 bIsMoving==false 일 때 호출한다. CanMoveTo 실패 시 해당 Dir drop. */
	void AdvanceReplayStep();

#ifdef _DEBUG
	/**
	 * F8 디버그 시연 — 한 프레임 안에 C_DEBUG_FORCE_REJECT(reject@3, bypass=5) 1 건 + Right/Right/Up/Up/Right 5 건의 C_MOVE 를
	 * 끊김 없이 송신하여 결정론적 reject/replay 사이클을 1 회 재현한다. cooldown/필터/InputManager 키 검사를 모두 우회한다.
	 * 패킷은 즉시 5 건 송신되지만 클라 보간은 PendingDebugBurstDirs 로 분산되어 매 프레임 한 타일씩 진행 — 영상 가독성 확보.
	 */
	void TriggerDebugReplayBurstSimulation();

	/** F8 시퀀스의 한 패킷 송신 헬퍼 — cooldown/필터/InputManager 검사를 우회하고 C_MOVE 1 건 + SnapshotQueue 1 건만 추가. 보간은 큐에 위임. */
	void DebugSendMoveBurstStep(Protocol::Direction Dir);

	/** PendingDebugBurstDirs front 1 건을 소비해 한 타일 BeginMoveTo. AdvanceReplayStep 과 같은 패턴, 단 4 배속 증폭은 적용 안 함. */
	void AdvanceDebugBurstStep();
#endif

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

#ifdef _DEBUG
	/** [_DEBUG] F9 토글로 켜지는 이동 동기화 시연 모드. ON 이면 클라 사전 필터(World->CanMoveTo) 만 우회하여 충돌 타일 안으로 1칸 예측 진입 + C_MOVE 송신을 허용한다. 서버는 ValidateMove 실패로 reject → 1칸 워프 보정. cooldown 은 그대로 적용되고 LastServerAcceptMs 갱신이 일어나지 않으므로 desync 가 누적되지 않는다 — 정상 게임 중 자연 발생하는 짧은 reconcile 모습. */
	bool bMoveSyncDebugIgnoreFilter = false;

	/** [_DEBUG] F8 토글로 켜지는 이동 시뮬 슬로우 모션. ON 이면 정상 보간/리플레이 모두 DebugSlowMotionMultiplier 만큼 감속하여 영상 촬영 시 롤백/4 배속 replay 가 눈에 잘 들어오도록 한다. 토글 ON 순간에 burst 시뮬 1 회 자동 발동. */
	bool bDebugSlowMotion = false;

	/** [_DEBUG] F8 burst 시뮬 보간 큐. 패킷은 즉시 5 건 송신했지만 BeginMoveTo 는 매 프레임 한 타일씩 분산 소비하여 (3,2) 까지 누적 예측 모습이 화면에 보이도록 한다. */
	std::vector<Protocol::Direction> PendingDebugBurstDirs;

	/** [_DEBUG] F8 burst 진행 중 도착한 S_MOVE_REJECT 를 deferred 처리하기 위한 보관. burst 보간이 (3,2) 까지 완료된 후에야 본체 OnServerMoveRejected 가 호출되도록 한다 — 영상에 (3,2) 도달 → (2,0) 워프 → replay 흐름이 또렷이 잡히도록. */
	bool bHasDeferredReject = false;
	uint64 DeferredRejectSeq = 0;
	uint64 DeferredRejectLastAcceptedSeq = 0;
	int32 DeferredRejectServerTileX = 0;
	int32 DeferredRejectServerTileY = 0;
#endif

	/** 연속 reject 횟수. replay 성공(OnServerMoveAccepted 정상 pop) 시 0 으로 리셋. MaxReconcileRetries 초과 시 warp-only 로 fallback. */
	int32 ReconcileRetryCount = 0;

	/** reject/mismatch 수신 시 StageReplayFromSnapshots 로 채워지는 Dir 대기열. Tick 이 매 프레임 한 건씩 소비. */
	std::vector<Protocol::Direction> PendingReplayDirs;

	/** replay 진행 중 플래그. 진입 시 MoveSpeed 를 ReplayMoveSpeedMultiplier 배로 증폭, 종료 시 원복. 엣지 감지용이라 한 번씩만 토글된다. */
	bool bInReplayMode = false;

	/** 플레이어 머리 위에 렌더되는 체력바. APlayerActor 가 소유(World Actor 아님). */
	std::unique_ptr<UHealthBarWidget> HealthBar;

	// ---------------------------------------------------------------
	// Directional 모드 전용 (legacy 모드면 bUseDirectionalSprite=false 로 미사용)
	// ---------------------------------------------------------------

	/** 신규 4방향 멀티파일 모드 사용 여부. true 면 Render 가 CurrentFacing 에 따라 텍스처를 동적 교체. */
	bool bUseDirectionalSprite = false;

	/** [State(Idle/Walk/Attack)][Facing(Down/Left/Right/Up)] = 12장 캐싱. ctor 에서 ResourceManager 조회 1회로 채움. */
	std::shared_ptr<Texture> DirectionalTextures[3][4];

	/** Sprite 출력 크기. Render 에서 Sprite::SetRenderSize 재적용 시 참조 (legacy 베이스의 RenderSize=64 와 분리). */
	int32 DirectionalRenderW = 64;
	int32 DirectionalRenderH = 64;

	/**
	 * Directional 스프라이트의 세로 위치 보정값(픽셀, 양수 = 아래로 내림).
	 * FREE_Adventurer 96×80 프레임은 캐릭터 아래에 빈 여백이 있어 RenderH 만큼 그대로 올리면(=OffsetY = TileSize - RenderH)
	 * 캐릭터가 타일보다 한참 위로 떠 보인다. 또한 legacy 모드(monster 등) 와 시각적 정렬을 맞추려면 발끝 정확 정렬보다 살짝 떠 있는 편이 더 자연스럽다.
	 * 현재 값은 시각 검수로 결정한 단일 매직 넘버 — 다른 에셋으로 교체할 때 이 값 하나만 조정하면 된다.
	 */
	static constexpr int32 DirectionalSpriteVerticalNudge = 40;

	// ---------------------------------------------------------------
	// Mirror 모드 전용 (legacy/directional 이면 bUseMirrorSprite=false)
	// ---------------------------------------------------------------

	/** Mirror 모드 사용 여부. true 면 Render 가 클립 ID(Idle/Walk/Attack)에 따라 텍스처를 동적 교체하고 bFacingLeft 로 좌우 미러. */
	bool bUseMirrorSprite = false;

	/** [State(Idle/Walk/Attack)] = 3장 캐싱. ctor 에서 ResourceManager 조회 1회로 채움. */
	std::shared_ptr<Texture> MirrorTextures[3];

	/** Mirror 모드 출력 크기. */
	int32 MirrorRenderW = 64;
	int32 MirrorRenderH = 64;

	/** Mirror 스프라이트 세로 위치 보정 (Directional 과 동일 정책). 사무라이 96×96 은 거의 프레임 하단=발이라 0 으로 시작. */
	static constexpr int32 MirrorSpriteVerticalNudge = 0;

	/** 서버가 부여한 nameplate 표시 텍스트(사람=Account.Id, 봇=카운터). 비어 있으면 렌더 스킵. */
	std::wstring NameplateText;

	/** 픽셀/초 — 생성자에서 ACharacterActor::MoveSpeed 에 덮어쓴다. */
	static constexpr float PlayerMoveSpeed = 260.f;

	/** 서버 Level::DefaultPlayerTileMoveSpeed 와 맞춘 폴백 값. S_ENTER_GAME 수신 전 스폰 직후 잠깐 사용. */
	static constexpr float DefaultTileMoveSpeed = 4.0f;

	/** 한 번의 reject 이벤트 체인에서 허용하는 replay 재시도 최대치. 초과 시 warp-only fallback 으로 무한 루프 차단. */
	static constexpr int32 MaxReconcileRetries = 3;

	/** replay 중 MoveSpeed 증폭 배율. 클수록 빠르게 복구하지만 너무 크면 스르륵 감각이 사라진다. */
	static constexpr float ReplayMoveSpeedMultiplier = 4.0f;

	/** [_DEBUG] F8 슬로우 모션 ON 시 base MoveSpeed 에 곱하는 배수. 슬로우 정상 = 0.5x, 슬로우 replay = 0.5 * 4 = 2x — 영상 가독성 우선. */
	static constexpr float DebugSlowMotionMultiplier = 1.0f;

	/** 체력바 윗변에서 nameplate baseline 까지의 픽셀 거리. 위로 띄울수록 캐릭터와 텍스트가 분리되어 보인다. */
	static constexpr int32 NameplateOffsetAboveHealthBar = -10;
};