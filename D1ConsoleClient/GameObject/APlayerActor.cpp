// winsock2.h는 Windows.h보다 먼저 include돼야 하며, APlayerActor.h가 끌어오는 Protocol.pb.h
// 계열이 Windows.h를 포함할 수 있으므로 이 파일 최상단에 고정한다.
#include <winsock2.h>
#include "Core/CoreMinimal.h"

#include "APlayerActor.h"

#include "Iocp/Session.h"
#include "Network/ServerPacketHandler.h"

#include "Game.h"
#include "Input/InputManager.h"
#include "Render/ResourceManager.h"
#include "Render/Sprite.h"
#include "Render/Texture.h"
#include "UI/UHealthBarWidget.h"
#include "UI/UChatPanel.h"
#include "World/UWorld.h"
#include "UCollisionMap.h"

APlayerActor::APlayerActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY, const FCharacterSpriteConfig& InConfig)
	: PlayerID(InPlayerID)
{
	// 서버가 부여한 스폰 타일 좌표를 즉시 반영 (워프).
	WarpTo(SpawnTileX, SpawnTileY);

	// 플레이어 이동 속도를 베이스에 주입.
	MoveSpeed = PlayerMoveSpeed;

	// 초기 HP (실제 값은 서버가 S_PLAYER_DAMAGED 로 덮어쓴다).
	HP = 100;
	MaxHP = 100;

	ActorSprite = std::make_shared<Sprite>();

	// ---------------------------------------------------------------
	// 모드별 분기: Legacy (단일 시트 row-기반) vs Directional (4방향 멀티파일)
	// ---------------------------------------------------------------
	if (InConfig.Mode == FCharacterSpriteConfig::EMode::Directional)
	{
		// Directional 모드 — 12장 텍스처를 [State][Facing] 격자로 캐싱하고, 초기 텍스처는 Idle/Down 으로 세팅.
		bUseDirectionalSprite = true;
		DirectionalRenderW = InConfig.RenderW;
		DirectionalRenderH = InConfig.RenderH;

		// 12장 캐싱 — ResourceManager::GetTexture 는 단순 map 조회라 ctor 1회 호출로 충분.
		// 각 PNG 는 1행 N프레임 레이아웃이므로 Sprite 의 Row 는 항상 0 으로 등록한다.
		const FDirectionalClip* DirClips[3] = { &InConfig.IdleDirClip, &InConfig.WalkDirClip, &InConfig.AttackDirClip };
		for (int32 StateIdx = 0; StateIdx < 3; ++StateIdx)
		{
			DirectionalTextures[StateIdx][static_cast<int32>(ECharacterFacing::Down)]  = ResourceManager::Get().GetTexture(DirClips[StateIdx]->DownTextureName);
			DirectionalTextures[StateIdx][static_cast<int32>(ECharacterFacing::Left)]  = ResourceManager::Get().GetTexture(DirClips[StateIdx]->LeftTextureName);
			DirectionalTextures[StateIdx][static_cast<int32>(ECharacterFacing::Right)] = ResourceManager::Get().GetTexture(DirClips[StateIdx]->RightTextureName);
			DirectionalTextures[StateIdx][static_cast<int32>(ECharacterFacing::Up)]    = ResourceManager::Get().GetTexture(DirClips[StateIdx]->UpTextureName);
		}

		// 공격 사이클 길이 — Directional 모드는 AttackDirClip 의 FrameCount/Fps 사용.
		AttackDuration = static_cast<float>(InConfig.AttackDirClip.FrameCount) / InConfig.AttackDirClip.Fps;

		// 초기 Sprite 세팅: Idle/Down 텍스처로 시작, FrameW/H · RenderW/H 적용.
		ActorSprite->Init(DirectionalTextures[static_cast<int32>(EAnimClip::Idle)][static_cast<int32>(ECharacterFacing::Down)], InConfig.FrameW, InConfig.FrameH);
		ActorSprite->SetRenderSize(InConfig.RenderW, InConfig.RenderH);

		// 클립은 Row=0 고정 + 각 상태의 FrameCount/Fps 사용. 텍스처는 Render 에서 SetTexture 로 동적 교체되지만 클립 메타데이터는 상태별로 고정.
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { 0, InConfig.IdleDirClip.FrameCount,   InConfig.IdleDirClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { 0, InConfig.WalkDirClip.FrameCount,   InConfig.WalkDirClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { 0, InConfig.AttackDirClip.FrameCount, InConfig.AttackDirClip.Fps });
	}
	else if (InConfig.Mode == FCharacterSpriteConfig::EMode::Mirror)
	{
		// Mirror 모드 — 상태별 단일 시트 + bFacingLeft 좌우 미러 (Samurai 등).
		bUseDirectionalSprite = false;
		bUseMirrorSprite = true;
		MirrorRenderW = InConfig.RenderW;
		MirrorRenderH = InConfig.RenderH;

		// 3장 캐싱 — Idle/Walk/Attack 슬롯에 직접 매핑.
		const FMirrorClip* MirrorClips[3] = { &InConfig.IdleMirrorClip, &InConfig.WalkMirrorClip, &InConfig.AttackMirrorClip };
		for (int32 StateIdx = 0; StateIdx < 3; ++StateIdx)
		{
			MirrorTextures[StateIdx] = ResourceManager::Get().GetTexture(MirrorClips[StateIdx]->TextureName);
		}

		// 공격 사이클 길이 — Mirror 모드는 AttackMirrorClip 사용.
		AttackDuration = static_cast<float>(InConfig.AttackMirrorClip.FrameCount) / InConfig.AttackMirrorClip.Fps;

		// 초기 Sprite 세팅: Idle 텍스처로 시작, FrameW/H · RenderW/H 적용.
		ActorSprite->Init(MirrorTextures[static_cast<int32>(EAnimClip::Idle)], InConfig.FrameW, InConfig.FrameH);
		ActorSprite->SetRenderSize(InConfig.RenderW, InConfig.RenderH);

		// 클립은 Row=0 고정 + 각 상태의 FrameCount/Fps. 텍스처는 Render 에서 동적 교체.
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { 0, InConfig.IdleMirrorClip.FrameCount,   InConfig.IdleMirrorClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { 0, InConfig.WalkMirrorClip.FrameCount,   InConfig.WalkMirrorClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { 0, InConfig.AttackMirrorClip.FrameCount, InConfig.AttackMirrorClip.Fps });
	}
	else
	{
		// Legacy 모드 — 기존 row-기반 단일 시트 (Dwarf 호환).
		bUseDirectionalSprite = false;

		// 공격 사이클 길이 — legacy 는 AttackClip 사용.
		AttackDuration = static_cast<float>(InConfig.AttackClip.Frames) / InConfig.AttackClip.Fps;

		// 시트는 32×32 프레임이지만, 화면에는 RenderSize(=64) 로 확대 출력한다.
		auto Texture = ResourceManager::Get().GetTexture(InConfig.TextureName);
		ActorSprite->Init(Texture, TileSize);
		ActorSprite->SetRenderSize(RenderSize);

		// Config 에 담긴 3종 클립을 그대로 Sprite 에 주입 — 파생 캐릭터마다 Row 는 같아도 Frames 수가 달라질 수 있음.
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { InConfig.IdleClip.Row,   InConfig.IdleClip.Frames,   InConfig.IdleClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { InConfig.WalkClip.Row,   InConfig.WalkClip.Frames,   InConfig.WalkClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { InConfig.AttackClip.Row, InConfig.AttackClip.Frames, InConfig.AttackClip.Fps });
	}

	ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));

	// 체력바 위젯 생성 — 시트 텍스처는 HealthBarSheet 로 등록돼 있다.
	HealthBar = std::make_unique<UHealthBarWidget>();
	HealthBar->SetHP(HP, MaxHP);
}

APlayerActor::~APlayerActor() = default;

bool APlayerActor::IsLocallyControlled() const
{
	// Game::MyPlayerID 는 S_ENTER_GAME 수신 시 설정되며 이후 변하지 않는다.
	const Game* Instance = Game::GetInstance();
	if (Instance == nullptr)
		return false;
	return PlayerID != 0 && PlayerID == Instance->GetMyPlayerID();
}

void APlayerActor::Tick(float DeltaTime)
{
	if (IsLocallyControlled())
	{
#ifdef _DEBUG
		// F9 토글 — 이동 동기화 시연 모드(클라 사전 필터 우회). 영상 촬영용. 채팅 패널 활성 여부와 무관하게 토글 가능하도록 입력 가드보다 위에 둔다.
		if (InputManager::Get().GetKeyDown(EKey::F9))
		{
			bMoveSyncDebugIgnoreFilter = !bMoveSyncDebugIgnoreFilter;
			if (Game* Instance = Game::GetInstance())
			{
				Instance->AddDebugLog(bMoveSyncDebugIgnoreFilter ? L"[Debug] MoveSync FilterBypass=ON" : L"[Debug] MoveSync FilterBypass=OFF");
			}
		}

		// F8 토글 — ON 으로 전환되는 순간 슬로우 모션 진입 + burst 시뮬 1 회 자동 발동, OFF 로 전환되면 슬로우 해제만 수행.
		// 영상 촬영용이라 한 토글 사이클 동안 burst 1 회 + 슬로우 보간을 함께 보여주고, OFF 후엔 정상 입력 플레이로 복귀한다.
		if (InputManager::Get().GetKeyDown(EKey::F8))
		{
			bDebugSlowMotion = !bDebugSlowMotion;
			if (Game* Instance = Game::GetInstance())
			{
				Instance->AddDebugLog(bDebugSlowMotion ? L"[Debug] SlowMotion=ON + burst trigger" : L"[Debug] SlowMotion=OFF");
			}
			if (bDebugSlowMotion)
			{
				TriggerDebugReplayBurstSimulation();
			}
		}
#endif

		// 정상/replay 모드별 MoveSpeed 결정. base 는 PlayerMoveSpeed, 슬로우 모션 ON 이면 곱하기 0.5x. replay 모드면 그 위에 4x.
		// 매 프레임 갱신해서 슬로우 토글을 즉시 반영한다 — 엣지 감지 대신 idempotent 한 직접 대입.
		float BaseSpeed = PlayerMoveSpeed;
#ifdef _DEBUG
		if (bDebugSlowMotion)
		{
			BaseSpeed *= DebugSlowMotionMultiplier;
		}
#endif
		const bool bShouldReplay = PendingReplayDirs.empty() == false || (bInReplayMode && bIsMoving);
		bInReplayMode = bShouldReplay;
		MoveSpeed = bShouldReplay ? (BaseSpeed * ReplayMoveSpeedMultiplier) : BaseSpeed;

		// 보간 또는 공격 중이 아닐 때만 다음 동작 결정. 
		if (!bIsMoving && !bIsAttacking)
		{
			if (PendingReplayDirs.empty() == false)
			{
				// 매 프레임마다 빠른 이동속도로 보간 진행
				AdvanceReplayStep(); 
			}
#ifdef _DEBUG
			else if (PendingDebugBurstDirs.empty() == false)
			{
				AdvanceDebugBurstStep();
			}
			else if (bHasDeferredReject)
			{
				// 캐릭터가 (3,2) 까지 도달한 직후 — 큐잉해 두었던 reject 본체를 호출해 (2,0) 워프 + replay 시작.
				const uint64 DefSeq = DeferredRejectSeq;
				const uint64 DefLastAccepted = DeferredRejectLastAcceptedSeq;
				const int32 DefX = DeferredRejectServerTileX;
				const int32 DefY = DeferredRejectServerTileY;
				bHasDeferredReject = false;
				OnServerMoveRejected(DefSeq, DefLastAccepted, DefX, DefY);
			}
#endif
			else
			{
				ProcessInput();
			}
		}
	}

	// 이동/공격/애니메이션/스프라이트 업데이트는 베이스가 전담 — MoveSpeed 증폭이 여기서 4 배속 보간으로 반영된다.
	ACharacterActor::Tick(DeltaTime);

	if (HealthBar)
		HealthBar->SetHP(HP, MaxHP);
}

void APlayerActor::Render(HDC BackDC)
{
	// 1. 캐릭터 본체 렌더
	//    Directional 모드는 CurrentFacing+현재 클립 상태에 따라 텍스처를 동적 교체한 후 직접 그린다.
	//    Legacy 모드는 기존 베이스 렌더(중앙 정렬 + bFacingLeft 미러)를 그대로 사용.
	int32 SpriteRenderH = RenderSize; // HealthBar 앵커 계산에 사용 (legacy 기본값)
	if (bUseMirrorSprite && ActorSprite)
	{
		// 현재 클립(Idle/Walk/Attack) → State 인덱스. 클립 미설정이면 Idle 폴백.
		const int32 ClipId = ActorSprite->GetCurrentClip();
		int32 StateIdx = static_cast<int32>(EAnimClip::Idle);
		if (ClipId == static_cast<int32>(EAnimClip::Walk))        StateIdx = static_cast<int32>(EAnimClip::Walk);
		else if (ClipId == static_cast<int32>(EAnimClip::Attack)) StateIdx = static_cast<int32>(EAnimClip::Attack);

		if (MirrorTextures[StateIdx])
		{
			ActorSprite->SetTexture(MirrorTextures[StateIdx]);
		}

		// 가로는 타일 중앙. 세로는 Directional 과 동일 정책 — 프레임 하단 여백 보정 후 그린다.
		const int32 OffsetX = (TileSize - MirrorRenderW) / 2;
		const int32 OffsetY = TileSize - MirrorRenderH + MirrorSpriteVerticalNudge;
		// Mirror 모드는 시트가 오른쪽만 향하므로 bFacingLeft 일 때 GDI 좌우 미러로 좌향 표현.
		ActorSprite->Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, /*bFlipH=*/bFacingLeft);

		SpriteRenderH = MirrorRenderH - MirrorSpriteVerticalNudge;
	}
	else if (bUseDirectionalSprite && ActorSprite)
	{
		// 현재 클립(Idle/Walk/Attack) → State 인덱스. 클립 미설정이면 Idle 폴백.
		const int32 ClipId = ActorSprite->GetCurrentClip();
		int32 StateIdx = static_cast<int32>(EAnimClip::Idle);
		if (ClipId == static_cast<int32>(EAnimClip::Walk))        StateIdx = static_cast<int32>(EAnimClip::Walk);
		else if (ClipId == static_cast<int32>(EAnimClip::Attack)) StateIdx = static_cast<int32>(EAnimClip::Attack);

		const int32 FacingIdx = static_cast<int32>(CurrentFacing);
		if (DirectionalTextures[StateIdx][FacingIdx])
		{
			ActorSprite->SetTexture(DirectionalTextures[StateIdx][FacingIdx]);
		}

		// 가로는 타일 중앙. 세로는 RenderH 그대로 올리면 프레임 하단 여백 때문에 너무 높게 떠 보이므로
		// DirectionalSpriteVerticalNudge 만큼 아래로 끌어내려 monster 등 legacy 캐릭터와 시각적 정렬을 맞춘다.
		// (값은 사용자 시각 검수로 결정 — 다른 에셋 교체 시 이 상수만 조정.)
		const int32 OffsetX = (TileSize - DirectionalRenderW) / 2;
		const int32 OffsetY = TileSize - DirectionalRenderH + DirectionalSpriteVerticalNudge;
		// Directional 모드는 좌/우 별도 파일을 쓰므로 GDI 미러링은 항상 비활성.
		ActorSprite->Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, /*bFlipH=*/false);

		// 체력바/네임플레이트는 스프라이트 상단(=Y+OffsetY) 위로 띄우도록 RenderH - Nudge 를 전달.
		SpriteRenderH = DirectionalRenderH - DirectionalSpriteVerticalNudge;
	}
	else
	{
		ACharacterActor::Render(BackDC);
	}

	// 2. 체력바 — 플레이어 머리 위 앵커. 캐릭터 발 위치(X,Y) 기준으로 중앙 위.
	const int32 AnchorX = static_cast<int32>(X) + TileSize / 2;
	const int32 AnchorY = static_cast<int32>(Y) + (TileSize - SpriteRenderH); // 스프라이트 상단 Y
	if (HealthBar)
	{
		HealthBar->Render(BackDC, AnchorX, AnchorY);
	}

	// 3. Nameplate — 체력바 위로 한 줄. 비어 있으면 스킵.
	//    GDI 정렬 상태를 일시 변경해 텍스트 중앙 + 하단 baseline 으로 출력 후 원복.
	if (NameplateText.empty() == false)
	{
		const int32 NameY = AnchorY - NameplateOffsetAboveHealthBar;
		const UINT OldAlign = ::GetTextAlign(BackDC);
		const COLORREF OldColor = ::SetTextColor(BackDC, RGB(255, 255, 255));
		const int32 OldBkMode = ::SetBkMode(BackDC, TRANSPARENT);
		::SetTextAlign(BackDC, TA_CENTER | TA_BOTTOM);
		::TextOutW(BackDC, AnchorX, NameY, NameplateText.c_str(), static_cast<int32>(NameplateText.size()));
		::SetTextAlign(BackDC, OldAlign);
		::SetTextColor(BackDC, OldColor);
		::SetBkMode(BackDC, OldBkMode);
	}
}

void APlayerActor::UpdateAnimationState()
{
	// 본인 캐릭터의 Walk 사이클은 타일 경계마다 bIsMoving 이 한 프레임 false 가 되었다 다시 true 가 되어
	// 베이스 구현이 Walk → Idle → Walk 토글을 일으키고, Sprite::SetClipId 가 클립 변경 시 frame=0 으로 리셋해
	// 애니메이션이 매 타일마다 끊겨 보인다. 이를 방지하기 위해 "이동 키가 눌려 있으면 Walk 강제 유지" 정책을 본인에게만 적용.
	if (ActorSprite == nullptr)
	{
		return;
	}
	if (bIsAttacking)
	{
		ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Attack));
		return;
	}
	if (bIsMoving)
	{
		ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Walk));
		return;
	}
	if (IsLocallyControlled())
	{
		// 채팅 입력 활성 시에는 키 입력이 채팅용이므로 Walk 강제 유지 금지 — 캐릭터가 가만히 있어야 한다.
		bool bChatActive = false;
		if (Game* Instance = Game::GetInstance())
		{
			if (UChatPanel* Panel = Instance->GetChatPanel())
			{
				bChatActive = Panel->IsInputActive();
			}
		}
		if (bChatActive == false)
		{
			const InputManager& Input = InputManager::Get();
			const bool bWantsToMove = Input.GetKey(EKey::W) || Input.GetKey(EKey::S) || Input.GetKey(EKey::A) || Input.GetKey(EKey::D);
			if (bWantsToMove)
			{
				ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Walk));
				return;
			}
		}
	}
	ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));
}

void APlayerActor::OnServerMoveRejected(uint64 RejectedSeq, uint64 LastAcceptedSeq, int32 ServerTileX, int32 ServerTileY)
{
#ifdef _DEBUG
	// F8 burst 가 아직 보간 진행 중이라면 reject 처리 자체를 보간 완료까지 deferred. 후속 ack 도 같이 무시되어
	// 영상에 (3,2) 까지 누적 예측 → 보간 완료 → (2,0) 워프 → replay 흐름이 끊김 없이 잡히도록 한다.
	if (IsLocallyControlled() && PendingDebugBurstDirs.empty() == false)
	{
		bHasDeferredReject = true;
		DeferredRejectSeq = RejectedSeq;
		DeferredRejectLastAcceptedSeq = LastAcceptedSeq;
		DeferredRejectServerTileX = ServerTileX;
		DeferredRejectServerTileY = ServerTileY;
		return;
	}
#endif

	// 1) 거절된 snapshot 및 그 이전까지의 잔여 항목을 front 에서 제거. 이후 큐에는 클라가 예측한 "이후" 입력만 남는다.
	while (!SnapshotQueue.empty() && SnapshotQueue.front().ClientSeq <= RejectedSeq)
		SnapshotQueue.erase(SnapshotQueue.begin());

	// 2) 서버가 알려 준 권위 좌표로 베이스 리셋. replay 는 이 좌표에서부터 시작한다.
	WarpTo(ServerTileX, ServerTileY);

	// 3) loop guard — 짧은 시간 안에 연쇄 reject 가 쏟아지면 replay 를 포기하고 warp-only 로 안정화.
	++ReconcileRetryCount;
	if (ReconcileRetryCount > MaxReconcileRetries)
	{
		SnapshotQueue.clear();
		ReconcileRetryCount = 0;
		return;
	}

	// 4) 남은 snapshot 들의 Dir 을 replay 대기열로 옮긴다. 실제 보간은 Tick 이 매 프레임 한 타일씩 진행.
	StageReplayFromSnapshots();
}

void APlayerActor::OnServerDamaged(int32 InHP, int32 InMaxHP)
{
	// HP 갱신 + Hit 이펙트 스폰은 베이스가 처리.
	ACharacterActor::OnServerDamaged(InHP, InMaxHP);

	// 체력바는 Tick 에서도 갱신되지만 즉시 반영되도록 여기서도 호출.
	if (HealthBar)
		HealthBar->SetHP(HP, MaxHP);
}

void APlayerActor::ProcessInput()
{
	// 채팅 입력 활성 중에는 캐릭터 이동/공격 키를 모두 무시 — 채팅 키와 게임 키가 같은 키보드 이벤트로 들어오므로 가드 필요.
	if (Game* Instance = Game::GetInstance())
	{
		if (UChatPanel* Panel = Instance->GetChatPanel())
		{
			if (Panel->IsInputActive())
			{
				return;
			}
		}
	}

	// 공격 입력은 이동보다 우선. 공격 중이면 이동 입력은 받지 않는다.
	if (InputManager::Get().GetKeyDown(EKey::Space))
	{
		HandleAttackInput();
		return;
	}
	HandleMoveInput();
}

void APlayerActor::HandleAttackInput()
{
	// 공격 상태 진입 — 애니메이션 재생 시작.
	BeginAttack();

	// C_ATTACK 송신. 실제 데미지 적용은 서버 권위.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return;
	const SessionRef& Client = Instance->GetClientSession();
	if (Client == nullptr) return;

	Protocol::C_ATTACK AttackPkt;
	Client->Send(ServerPacketHandler::MakeSendBuffer(AttackPkt));
}

void APlayerActor::HandleMoveInput()
{
	InputManager& Input = InputManager::Get();

	// WASD 입력 → 서버 프로토콜 Direction 변환. 대각 없음, 우선순위 W>S>A>D.
	// CurrentFacing 도 입력 즉시 갱신 — 이동이 cooldown/CanMoveTo 로 막혀도 4방향 텍스처가 즉시 반응한다(bFacingLeft 와 동일 정책).
	Protocol::Direction Dir = Protocol::DIR_UP;
	int32 DeltaX = 0;
	int32 DeltaY = 0;
	bool bHasInput = false;
	if      (Input.GetKey(EKey::W)) { Dir = Protocol::DIR_UP;    DeltaY = -1; bHasInput = true; CurrentFacing = ECharacterFacing::Up; }
	else if (Input.GetKey(EKey::S)) { Dir = Protocol::DIR_DOWN;  DeltaY =  1; bHasInput = true; CurrentFacing = ECharacterFacing::Down; }
	else if (Input.GetKey(EKey::A)) { Dir = Protocol::DIR_LEFT;  DeltaX = -1; bHasInput = true; bFacingLeft = true;  CurrentFacing = ECharacterFacing::Left; }
	else if (Input.GetKey(EKey::D)) { Dir = Protocol::DIR_RIGHT; DeltaX =  1; bHasInput = true; bFacingLeft = false; CurrentFacing = ECharacterFacing::Right; }

	if (bHasInput == false)
		return;

	// 로컬 쿨다운 — 서버가 강제하는 쿨다운(TileMoveSpeed)을 클라도 동일하게 적용해 정상 플레이어가 reject 받는 일을 없앤다.
	// 첫 이동(LastMoveTimeMs == 0)은 면제. 디버그 시연 모드에서도 cooldown 자체는 정상 적용 — 우회는 송신 단계에서만 한다.
	const uint64 NowMs = ::GetTickCount64();
	const float CooldownMs = 1000.0f / TileMoveSpeed;
	if (LastMoveTimeMs != 0 && static_cast<float>(NowMs - LastMoveTimeMs) < CooldownMs)
		return;

	// 사전 필터: 로컬 CanMoveTo 를 통과하지 못하면 서버 왕복 생략.
	// 디버그 시연 모드에서는 이 필터를 우회해서 충돌 타일 안으로 1칸 잘못 예측 + C_MOVE 송신 → 서버 ValidateMove 실패 → reject 보정 사이클이 자연 발생한다.
	const int32 NextTileX = GetTileX() + DeltaX;
	const int32 NextTileY = GetTileY() + DeltaY;
	if (World == nullptr)
		return;
	bool bSkipClientFilter = false;
#ifdef _DEBUG
	bSkipClientFilter = bMoveSyncDebugIgnoreFilter;
#endif
	if (!bSkipClientFilter && World->CanMoveTo(this, NextTileX, NextTileY) == false)
		return;

	// Client Prediction: 서버 응답을 기다리지 않고 즉시 로컬 이동을 시작하고 C_MOVE 를 보낸다.
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return;
	const SessionRef& Client = Instance->GetClientSession();
	if (Client == nullptr) return;

	const uint64 ClientSeq = NextClientSeq++;

	// 1) 스냅샷 큐에 예측 이동을 기록. 서버의 S_MOVE(일치 seq) 시 pop, S_MOVE_REJECT 시 clip 기준으로 사용.
	FMoveSnapshot Snapshot;
	Snapshot.ClientSeq = ClientSeq;
	Snapshot.TileX = NextTileX;
	Snapshot.TileY = NextTileY;
	Snapshot.Dir = Dir;
	SnapshotQueue.push_back(Snapshot);

	// 2) 로컬 예측 — 즉시 타일 갱신 + 픽셀 보간 시작.
	BeginMoveTo(NextTileX, NextTileY);

	// 3) 직전 송신 후 경과 ms 캡처(첫 패킷은 0). LastMoveTimeMs 갱신 전에 계산해야 0이 박히지 않는다.
	const uint64 ClientDeltaMs = (LastMoveTimeMs == 0) ? 0 : (NowMs - LastMoveTimeMs);

	// 4) 입력 쿨다운 타이머 갱신. 서버와 동일한 기준이므로 reject 가 일어나지 않는다.
	LastMoveTimeMs = NowMs;

	// 5) C_MOVE 송신 — Direction + ClientSeq + ClientDelta. 서버 cooldown 검증은 서버시간 단독, ClientDelta 는 참고용.
	Protocol::C_MOVE MovePkt;
	MovePkt.set_dir(Dir);
	MovePkt.set_client_seq(ClientSeq);
	MovePkt.set_client_delta_ms(ClientDeltaMs);
	Client->Send(ServerPacketHandler::MakeSendBuffer(MovePkt));
}

void APlayerActor::OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir, uint64 ClientSeq)
{
	// Client Prediction — 본인 예측 경로.
	if (IsLocallyControlled())
	{
#ifdef _DEBUG
		// F8 burst 보간 진행 중이거나 reject 처리가 보류 상태라면 ack 를 그대로 폐기 — burst 완료 후 deferred reject 가 PendingReplayDirs 를
		// 잔여 Dir 로 채워 동일 결과를 만든다. 여기서 ack 를 처리하면 SnapshotQueue 또는 replay 큐가 미리 비워져 워프가 발생할 수 있다.
		if (PendingDebugBurstDirs.empty() == false || bHasDeferredReject)
		{
			return;
		}
#endif

		// 수락된 seq 까지의 잔여 snapshot 을 front 에서 일괄 정리.
		const bool bFrontMatched = !SnapshotQueue.empty() && SnapshotQueue.front().ClientSeq == ClientSeq;
		while (!SnapshotQueue.empty() && SnapshotQueue.front().ClientSeq <= ClientSeq)
			SnapshotQueue.erase(SnapshotQueue.begin());

		if (bFrontMatched)
		{
			// 정상 — 예측과 서버 확정이 일치. 연쇄 reject 카운터 리셋.
			ReconcileRetryCount = 0;
			return;
		}

		// 예측 누락 케이스 — 서버 권위 좌표로 리셋 후 남은 예측 움직임을 Dir replay 대기열에 적재.
		WarpTo(NextTileX, NextTileY);
		if (Dir == Protocol::DIR_LEFT)       bFacingLeft = true;
		else if (Dir == Protocol::DIR_RIGHT) bFacingLeft = false;
		StageReplayFromSnapshots();
		return;
	}

	// 타인 이동 — 기존 경로: 타일 갱신 + 픽셀 보간 시작.
	BeginMoveTo(NextTileX, NextTileY);
	if (Dir == Protocol::DIR_LEFT)       bFacingLeft = true;
	else if (Dir == Protocol::DIR_RIGHT) bFacingLeft = false;
}

void APlayerActor::ClearSnapshotQueue()
{
	SnapshotQueue.clear();
	PendingReplayDirs.clear();
#ifdef _DEBUG
	PendingDebugBurstDirs.clear();
	bHasDeferredReject = false;
#endif
	ReconcileRetryCount = 0;
	// replay 중이었다면 MoveSpeed 도 즉시 원복. 다음 Tick 까지 기다리지 않는다.
	if (bInReplayMode)
	{
		bInReplayMode = false;
		MoveSpeed = PlayerMoveSpeed;
	}
}

void APlayerActor::StageReplayFromSnapshots()
{
	// SnapshotQueue 의 Dir 만 뽑아 PendingReplayDirs 로 복사. SnapshotQueue 는 서버 seq 동기 기준이 깨졌으므로 비운다.
	PendingReplayDirs.clear();
	PendingReplayDirs.reserve(SnapshotQueue.size());
	for (const FMoveSnapshot& Snap : SnapshotQueue)
		PendingReplayDirs.push_back(Snap.Dir);
	SnapshotQueue.clear();
}

#ifdef _DEBUG
void APlayerActor::TriggerDebugReplayBurstSimulation()
{
	// 결정론 시연 시퀀스: 우, 우, 상, 상, 우 (5 패킷) — 시작 좌표 기준 (3,2) 까지 누적 예측 후 3 번째 (상) 강제 reject.
	static constexpr Protocol::Direction BurstSequence[] = {
		Protocol::DIR_RIGHT,
		Protocol::DIR_RIGHT,
		Protocol::DIR_UP,
		Protocol::DIR_UP,
		Protocol::DIR_RIGHT,
	};
	static constexpr uint32 BurstPacketCount = static_cast<uint32>(sizeof(BurstSequence) / sizeof(BurstSequence[0]));
	static constexpr uint32 RejectAtNthPacket = 3;

	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return;
	const SessionRef& Client = Instance->GetClientSession();
	if (Client == nullptr) return;

	// 1) 서버에 강제 reject + cooldown 면제 카운터를 미리 세팅. 후속 5 개의 C_MOVE 가 도착하기 전에 Job 큐 앞단에 들어가도록 가장 먼저 송신.
	Protocol::C_DEBUG_FORCE_REJECT DebugPkt;
	DebugPkt.set_reject_at_nth_packet(RejectAtNthPacket);
	DebugPkt.set_cooldown_bypass_count(BurstPacketCount);
	Client->Send(ServerPacketHandler::MakeSendBuffer(DebugPkt));

	Instance->AddDebugLog(L"[Debug] F8 burst start (RRUUR, reject@3)");

	// 2) 5 패킷을 한 프레임 안에 즉시 송신. 클라 cooldown/필터 검사를 모두 우회한다 — 서버는 위에서 세팅한 카운터로 burst 를 받아낸다.
	for (uint32 Index = 0; Index < BurstPacketCount; ++Index)
	{
		DebugSendMoveBurstStep(BurstSequence[Index]);
	}
}

void APlayerActor::DebugSendMoveBurstStep(Protocol::Direction Dir)
{
	Game* Instance = Game::GetInstance();
	if (Instance == nullptr) return;
	const SessionRef& Client = Instance->GetClientSession();
	if (Client == nullptr) return;
	if (World == nullptr) return;

	// 방향 → 델타 산출용 — SnapshotQueue 의 예측 타일 좌표 기록에만 사용. facing/실제 보간은 AdvanceDebugBurstStep 가 큐 소비 시점에 갱신한다.
	int32 DeltaX = 0;
	int32 DeltaY = 0;
	switch (Dir)
	{
	case Protocol::DIR_UP:    DeltaY = -1; break;
	case Protocol::DIR_DOWN:  DeltaY =  1; break;
	case Protocol::DIR_LEFT:  DeltaX = -1; break;
	case Protocol::DIR_RIGHT: DeltaX =  1; break;
	default: return;
	}

	// SnapshotQueue 는 reject reconcile 정합성을 위해 예측 좌표 기준으로 누적한다 — burst 끝 좌표(=직전 큐 항목 다음 칸) 를 기록.
	int32 PredictedTileX;
	int32 PredictedTileY;
	if (SnapshotQueue.empty())
	{
		PredictedTileX = GetTileX() + DeltaX;
		PredictedTileY = GetTileY() + DeltaY;
	}
	else
	{
		const FMoveSnapshot& Last = SnapshotQueue.back();
		PredictedTileX = Last.TileX + DeltaX;
		PredictedTileY = Last.TileY + DeltaY;
	}

	const uint64 ClientSeq = NextClientSeq++;

	FMoveSnapshot Snapshot;
	Snapshot.ClientSeq = ClientSeq;
	Snapshot.TileX = PredictedTileX;
	Snapshot.TileY = PredictedTileY;
	Snapshot.Dir = Dir;
	SnapshotQueue.push_back(Snapshot);

	// 보간은 한 프레임에 한 타일씩 분산 — 큐에 Dir 만 적재하고 Tick 의 AdvanceDebugBurstStep 가 BeginMoveTo 를 호출한다.
	PendingDebugBurstDirs.push_back(Dir);

	LastMoveTimeMs = ::GetTickCount64();

	Protocol::C_MOVE MovePkt;
	MovePkt.set_dir(Dir);
	MovePkt.set_client_seq(ClientSeq);
	MovePkt.set_client_delta_ms(0);
	Client->Send(ServerPacketHandler::MakeSendBuffer(MovePkt));
}

void APlayerActor::AdvanceDebugBurstStep()
{
	const Protocol::Direction Dir = PendingDebugBurstDirs.front();
	PendingDebugBurstDirs.erase(PendingDebugBurstDirs.begin());

	int32 DeltaX = 0;
	int32 DeltaY = 0;
	switch (Dir)
	{
	case Protocol::DIR_UP:    DeltaY = -1; break;
	case Protocol::DIR_DOWN:  DeltaY =  1; break;
	case Protocol::DIR_LEFT:  DeltaX = -1; bFacingLeft = true;  break;
	case Protocol::DIR_RIGHT: DeltaX =  1; bFacingLeft = false; break;
	default: return;
	}

	const int32 NextX = GetTileX() + DeltaX;
	const int32 NextY = GetTileY() + DeltaY;
	// burst 시연은 의도적으로 사전 필터 우회한 상태이므로 CanMoveTo 검사 없이 그대로 보간 시작 — 예측이 (3,2) 까지 누적되는 모습이 화면에 보이게 한다.
	BeginMoveTo(NextX, NextY);
}
#endif

void APlayerActor::AdvanceReplayStep()
{
	// 대기열 front 한 건만 꺼내 한 타일 이동을 시작한다. 보간은 ACharacterActor::UpdateMovement 가 이번 Tick 의 MoveSpeed(증폭된 값)로 진행.
	const Protocol::Direction Dir = PendingReplayDirs.front();
	PendingReplayDirs.erase(PendingReplayDirs.begin());

	int32 DeltaX = 0;
	int32 DeltaY = 0;
	switch (Dir)
	{
	case Protocol::DIR_UP:    DeltaY = -1; break;
	case Protocol::DIR_DOWN:  DeltaY =  1; break;
	case Protocol::DIR_LEFT:  DeltaX = -1; bFacingLeft = true;  break;
	case Protocol::DIR_RIGHT: DeltaX =  1; bFacingLeft = false; break;
	default: return;
	}

	const int32 NextX = GetTileX() + DeltaX;
	const int32 NextY = GetTileY() + DeltaY;
	// 서버가 실제로 막았을 가능성이 있는 경로는 drop — 다음 프레임에 다음 Dir 로 넘어간다.
	if (World == nullptr || World->CanMoveTo(this, NextX, NextY) == false)
		return;

	BeginMoveTo(NextX, NextY);
}
