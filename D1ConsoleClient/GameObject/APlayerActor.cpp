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

APlayerActor::APlayerActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY) : PlayerID(InPlayerID)
{
	// 서버가 부여한 스폰 타일 좌표를 즉시 반영 (워프).
	WarpTo(SpawnTileX, SpawnTileY);

	// 플레이어 이동 속도를 베이스에 주입.
	MoveSpeed = PlayerMoveSpeed;

	// 초기 HP (실제 값은 서버가 S_PLAYER_DAMAGED 로 덮어쓴다).
	HP = 100;
	MaxHP = 100;

	// 스프라이트 초기화 — 텍스처는 LoadResources() 이후 이미 등록돼 있다.
	ActorSprite = std::make_shared<Sprite>();
	auto Texture = ResourceManager::Get().GetTexture(L"PlayerSprite");
	// 시트는 32×32 프레임이지만, 화면에는 RenderSize(=64) 로 확대 출력한다.
	ActorSprite->Init(Texture, TileSize);
	ActorSprite->SetRenderSize(RenderSize);

	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { IdleClip.Row,   IdleClip.Frames,   IdleClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { WalkClip.Row,   WalkClip.Frames,   WalkClip.Fps });
	ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { AttackClip.Row, AttackClip.Frames, AttackClip.Fps });
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
	// 1. Input 처리 — 로컬 플레이어만. 타일 보간/공격 중에는 차단 (bIsMoving 이 곧 in-flight 1 건 rate limit).
	if (IsLocallyControlled() && !bIsMoving && !bIsAttacking)
		ProcessInput();

	// 2. 이동/공격/애니메이션/스프라이트 업데이트는 베이스가 전담.
	ACharacterActor::Tick(DeltaTime);

	// 3. 체력바 갱신 — Tick 당 1회로 충분.
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

void APlayerActor::OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir, uint64 ClientSeq)
{
	// Client Prediction: 본인 예측과 매칭되는 S_MOVE 는 이미 BeginMoveTo 로 반영되었으므로 스냅샷만 pop.
	// seq 불일치 시에는 서버 권위로 정정 — BeginMoveTo 로 폴백.
	if (IsLocallyControlled())
	{
		if (!SnapshotQueue.empty() && SnapshotQueue.front().ClientSeq == ClientSeq)
		{
			SnapshotQueue.erase(SnapshotQueue.begin());
			return;
		}
		// 예측 누락 케이스 — 로컬 상태와 서버 권위를 강제로 맞춘다.
		SnapshotQueue.clear();
		WarpTo(NextTileX, NextTileY);
		if (Dir == Protocol::DIR_LEFT)       bFacingLeft = true;
		else if (Dir == Protocol::DIR_RIGHT) bFacingLeft = false;
		return;
	}

	// 타인 이동 — 기존 경로: 타일 갱신 + 픽셀 보간 시작.
	BeginMoveTo(NextTileX, NextTileY);
	if (Dir == Protocol::DIR_LEFT)       bFacingLeft = true;
	else if (Dir == Protocol::DIR_RIGHT) bFacingLeft = false;
}

void APlayerActor::OnServerMoveRejected(uint64 LastAcceptedSeq, int32 ServerTileX, int32 ServerTileY)
{
	// LastAcceptedSeq 이후의 예측 스냅샷은 무효 — 큐에서 drop.
	while (!SnapshotQueue.empty() && SnapshotQueue.back().ClientSeq > LastAcceptedSeq)
		SnapshotQueue.pop_back();

	// 서버 권위 좌표로 즉시 워프. (MVP — 롤백 lerp 는 nice-to-have 로 추후 작업.)
	WarpTo(ServerTileX, ServerTileY);
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

	// 3) C_MOVE 송신 — Direction + ClientSeq. 서버는 자체 TileX/Y 기준으로 검증하고 실패 시에만 회신한다.
	Protocol::C_MOVE MovePkt;
	MovePkt.set_dir(Dir);
	MovePkt.set_client_seq(ClientSeq);
	Client->Send(ServerPacketHandler::MakeSendBuffer(MovePkt));
}