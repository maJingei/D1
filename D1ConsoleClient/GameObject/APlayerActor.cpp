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
#include "UI/UHealthBarWidget.h"
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

	// Player 끼리는 같은 타일을 공유할 수 있다 — LoadBot 다량 접속 시 시각적 다양성 확보 + reject 폭증 방지.
	// 몬스터/정적 타일 충돌은 별도 경로(CollisionMap 및 Monster bBlocksMovement)로 여전히 유지된다.
	bBlocksMovement = false;

	// 공격 1사이클 재생 시간(초) — Config.AttackClip 기반으로 런타임 계산하여 member 에 cache.
	//   static 이었다면 모든 파생이 동일 값을 강제당했을 텐데, 멤버로 옮겨 시트별 프레임 수/FPS 차이를 그대로 반영.
	AttackDuration = static_cast<float>(InConfig.AttackClip.Frames) / InConfig.AttackClip.Fps;

	// 스프라이트 초기화 — 텍스처는 LoadResources() 이후 이미 등록돼 있다.
	ActorSprite = std::make_shared<Sprite>();
	auto Texture = ResourceManager::Get().GetTexture(InConfig.TextureName);
	// 시트는 32×32 프레임이지만, 화면에는 RenderSize(=64) 로 확대 출력한다.
	ActorSprite->Init(Texture, TileSize);
	ActorSprite->SetRenderSize(RenderSize);

	// Config 에 담긴 3종 클립을 그대로 Sprite 에 주입 — 파생 캐릭터마다 Row 는 같아도 Frames 수가 달라질 수 있음.
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { InConfig.IdleClip.Row,   InConfig.IdleClip.Frames,   InConfig.IdleClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { InConfig.WalkClip.Row,   InConfig.WalkClip.Frames,   InConfig.WalkClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { InConfig.AttackClip.Row, InConfig.AttackClip.Frames, InConfig.AttackClip.Fps });
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
		// replay 모드 진입/종료 엣지 감지 — MoveSpeed 토글을 한 번씩만 수행한다.
		// 진입 조건: 대기열에 Dir 이 남아 있거나, 이미 replay 로 시작한 한 타일 보간이 아직 진행 중일 때.
		const bool bShouldReplay = PendingReplayDirs.empty() == false || (bInReplayMode && bIsMoving);
		if (bShouldReplay && !bInReplayMode)
		{
			bInReplayMode = true;
			MoveSpeed = PlayerMoveSpeed * ReplayMoveSpeedMultiplier;
		}
		else if (!bShouldReplay && bInReplayMode)
		{
			bInReplayMode = false;
			MoveSpeed = PlayerMoveSpeed;
		}

		// 보간 또는 공격 중이 아닐 때만 다음 동작 결정. replay 우선 — 대기열을 비우고서야 플레이어 입력을 받는다.
		if (!bIsMoving && !bIsAttacking)
		{
			if (PendingReplayDirs.empty() == false)
				AdvanceReplayStep();
			else
				ProcessInput();
		}
	}

	// 이동/공격/애니메이션/스프라이트 업데이트는 베이스가 전담 — MoveSpeed 증폭이 여기서 4 배속 보간으로 반영된다.
	ACharacterActor::Tick(DeltaTime);

	if (HealthBar)
		HealthBar->SetHP(HP, MaxHP);
}

void APlayerActor::Render(HDC BackDC)
{
	// 1. 캐릭터 본체 렌더 (베이스 공용 오프셋)
	ACharacterActor::Render(BackDC);

	// 2. 체력바 — 플레이어 머리 위 앵커. 캐릭터 발 위치(X,Y) 기준으로 중앙 위.
	if (HealthBar)
	{
		const int32 AnchorX = static_cast<int32>(X) + TileSize / 2;
		const int32 AnchorY = static_cast<int32>(Y) + (TileSize - RenderSize); // 스프라이트 상단 Y
		HealthBar->Render(BackDC, AnchorX, AnchorY);
	}
}

void APlayerActor::OnServerMoveRejected(uint64 RejectedSeq, uint64 LastAcceptedSeq, int32 ServerTileX, int32 ServerTileY)
{
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
	Protocol::Direction Dir = Protocol::DIR_UP;
	int32 DeltaX = 0;
	int32 DeltaY = 0;
	bool bHasInput = false;
	if      (Input.GetKey(EKey::W)) { Dir = Protocol::DIR_UP;    DeltaY = -1; bHasInput = true; }
	else if (Input.GetKey(EKey::S)) { Dir = Protocol::DIR_DOWN;  DeltaY =  1; bHasInput = true; }
	else if (Input.GetKey(EKey::A)) { Dir = Protocol::DIR_LEFT;  DeltaX = -1; bHasInput = true; bFacingLeft = true; }
	else if (Input.GetKey(EKey::D)) { Dir = Protocol::DIR_RIGHT; DeltaX =  1; bHasInput = true; bFacingLeft = false; }

	if (bHasInput == false)
		return;

	// 로컬 쿨다운 — 서버가 강제하는 쿨다운(TileMoveSpeed)을 클라도 동일하게 적용해 정상 플레이어가 reject 받는 일을 없앤다.
	// 첫 이동(LastMoveTimeMs == 0)은 면제.
	const uint64 NowMs = ::GetTickCount64();
	const float CooldownMs = 1000.0f / TileMoveSpeed;
	if (LastMoveTimeMs != 0 && static_cast<float>(NowMs - LastMoveTimeMs) < CooldownMs)
		return;

	// 사전 필터: 로컬 CanMoveTo 를 통과하지 못하면 서버 왕복 생략.
	const int32 NextTileX = GetTileX() + DeltaX;
	const int32 NextTileY = GetTileY() + DeltaY;
	if (World == nullptr || World->CanMoveTo(this, NextTileX, NextTileY) == false)
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

	// 3) 입력 쿨다운 타이머 갱신. 서버와 동일한 기준이므로 reject 가 일어나지 않는다.
	LastMoveTimeMs = NowMs;

	// 4) C_MOVE 송신 — Direction + ClientSeq. 서버는 자체 TileX/Y 기준으로 검증하고 실패 시에만 회신한다.
	Protocol::C_MOVE MovePkt;
	MovePkt.set_dir(Dir);
	MovePkt.set_client_seq(ClientSeq);
	Client->Send(ServerPacketHandler::MakeSendBuffer(MovePkt));
}

void APlayerActor::OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir, uint64 ClientSeq)
{
	// Client Prediction — 본인 예측 경로.
	if (IsLocallyControlled())
	{
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
