// winsock2.h는 Windows.h보다 먼저 include돼야 하며, APlayerActor.h가 끌어오는 Protocol.pb.h
// 계열이 Windows.h를 포함할 수 있으므로 이 파일 최상단에 고정한다.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <Windows.h>

#include "APlayerActor.h"

#include "Iocp/Session.h"
#include "Network/ServerPacketHandler.h"

#include "Game.h"
#include "Input/InputManager.h"
#include "Render/ResourceManager.h"
#include "World/UWorld.h"
#include "UCollisionMap.h"

namespace D1
{
	APlayerActor::APlayerActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY) : PlayerID(InPlayerID)
	{
		// 스폰 좌표는 서버 권한. 여기서는 인자로 받은 타일 좌표를 그대로 초기 논리/픽셀 위치에 반영한다.
		TilePos = { static_cast<float>(SpawnTileX), static_cast<float>(SpawnTileY) };
		TargetPos = TilePos * static_cast<float>(TileSize);
		X = TargetPos.X;
		Y = TargetPos.Y;

		// 스프라이트 초기화 — 텍스처는 LoadResources() 이후 이미 등록되어 있다.
		// ActorSprite는 AnimActor 멤버 — 생성 후 Init/AddClip/SetClipId만 호출.
		ActorSprite = std::make_shared<Sprite>();
		auto Texture = ResourceManager::Get().GetTexture(L"PlayerSprite");

		// 시트는 32×32 프레임이지만, 화면에는 RenderSize(=64)로 확대 출력한다.
		ActorSprite->Init(Texture, TileSize);
		ActorSprite->SetRenderSize(RenderSize);

		// Clip 추가
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Idle),   { IdleClip.Row,   IdleClip.Frames,   IdleClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Walk),   { WalkClip.Row,   WalkClip.Frames,   WalkClip.Fps });
		ActorSprite->AddClip(static_cast<int32>(EAnimClip::Attack), { AttackClip.Row, AttackClip.Frames, AttackClip.Fps });

		ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));
	}

	bool APlayerActor::IsLocallyControlled() const
	{
		// Game::MyPlayerID 는 S_ENTER_GAME 수신 시 설정되며 이후 변하지 않는다.
		// Instance 가 nullptr 인 경우(Shutdown 이후 잔여 Tick 등)에는 안전하게 false 반환.
		const Game* Instance = Game::GetInstance();
		if (Instance == nullptr)
			return false;
		return PlayerID != 0 && PlayerID == Instance->GetMyPlayerID();
	}

	void APlayerActor::Tick(float DeltaTime)
	{
		// 1. Input 처리 — 로컬 플레이어만 입력을 받는다. 이동/공격/서버 응답 대기 중에는 추가 입력을 받지 않는다.
		if (IsLocallyControlled() && !bIsMoving && !bIsAttacking && !bIsPending)
			ProcessInput();

		// 2. 공격 타이머 진행. Sprite는 자동 루프하므로, AttackDuration 경과 시점에 직접 Idle로 복귀시킨다.
		if (bIsAttacking)
		{
			AttackTimer += DeltaTime;
			if (AttackTimer >= AttackDuration)
			{
				bIsAttacking = false;
				AttackTimer = 0.f;
			}
		}

		// 3. 타일 이동 처리 (공격 중이면 bIsMoving=false이므로 no-op)
		UpdateMovement(DeltaTime);

		// 상태 우선순위: Attack > Walk > Idle
		EAnimClip CurrentClip = EAnimClip::Idle;
		if (bIsAttacking)
			CurrentClip = EAnimClip::Attack;
		else if (bIsMoving)
			CurrentClip = EAnimClip::Walk;

		switch (CurrentClip)
		{
		case EAnimClip::Attack: ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Attack)); break;
		case EAnimClip::Walk:   ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Walk));   break;
		case EAnimClip::Idle:   ActorSprite->SetClipId(static_cast<int32>(EAnimClip::Idle));   break;
		}

		// AnimActor::Tick이 Sprite->Update(DeltaTime)를 호출한다.
		AnimActor::Tick(DeltaTime);
	}

	void APlayerActor::UpdateMovement(float DeltaTime)
	{
		if (!bIsMoving)
			return;

		FVector2D RenderPos = { X, Y };
		FVector2D Delta = TargetPos - RenderPos;

		float DistSq = Delta.LengthSquared();
		float Step = MoveSpeed * DeltaTime;

		if (Step * Step >= DistSq)
		{
			// 목표 도달: 위치 확정 및 이동 종료
			X = TargetPos.X;
			Y = TargetPos.Y;
			bIsMoving = false;
		}
		else
		{
			// 목표 방향으로 Step만큼 전진
			X += Delta.Normalized().X * Step;
			Y += Delta.Normalized().Y * Step;
		}
	}

	void APlayerActor::Render(HDC BackDC)
	{
		// 확대 스프라이트(RenderSize)를 타일 중앙(가로) + 발끝을 타일 하단에 정렬한다.
		// OffsetX는 음수: 가로 중앙을 맞추기 위해 좌측으로 (RenderSize-TileSize)/2 만큼 당김.
		// OffsetY는 음수: 발끝을 타일 하단에 맞추기 위해 위로 (RenderSize-TileSize) 만큼 끌어올림.
		constexpr int32 OffsetX = (TileSize - RenderSize) / 2;
		constexpr int32 OffsetY = TileSize - RenderSize;
		if (ActorSprite)
			ActorSprite->Render(BackDC, static_cast<int32>(X) + OffsetX, static_cast<int32>(Y) + OffsetY, bFacingLeft);
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
		// 공격 상태 진입. Tick이 AttackDuration 경과 후 Idle로 복귀시킨다.
		bIsAttacking = true;
		AttackTimer = 0.f;
	}

	void APlayerActor::HandleMoveInput()
	{
		InputManager& Input = InputManager::Get();

		// WASD 입력을 서버 프로토콜 Direction 으로 변환. 대각 없음, 우선순위 W>S>A>D.
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

		// 사전 필터: 로컬 CanMoveTo 를 통과하지 못하면 서버 왕복을 생략한다(입력 락도 걸지 않음).
		// 서버는 truth — 여기서 통과해도 서버가 거절할 수 있고, 그때는 S_MOVE_REJECT 로 락이 해제된다.
		const int32 NextTileX = GetTileX() + DeltaX;
		const int32 NextTileY = GetTileY() + DeltaY;
		if (World == nullptr || World->CanMoveTo(this, NextTileX, NextTileY) == false)
			return;

		// 서버로 C_MOVE 송신 + 입력 락. 실제 이동은 S_MOVE 수신 시 OnServerMoveAccepted 에서 시작.
		Game* Instance = Game::GetInstance();
		if (Instance == nullptr) return;
		const SessionRef& Client = Instance->GetClientSession();
		if (Client == nullptr) return;

		Protocol::C_MOVE MovePkt;
		MovePkt.set_dir(Dir);
		Client->Send(ServerPacketHandler::MakeSendBuffer(MovePkt));

		bIsPending = true;
	}

	void APlayerActor::OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir)
	{
		// 서버가 확정한 목적 타일로 TargetPos 를 세팅하고 보간을 시작한다.
		TilePos = { static_cast<float>(NextTileX), static_cast<float>(NextTileY) };
		TargetPos = TilePos * static_cast<float>(TileSize);
		bIsMoving = true;
		bIsPending = false;

		// 좌향 이동은 스프라이트 수평 반전. 상하 이동은 이전 Facing 유지.
		if (Dir == Protocol::DIR_LEFT)
			bFacingLeft = true;
		else if (Dir == Protocol::DIR_RIGHT)
			bFacingLeft = false;
	}

	void APlayerActor::OnServerMoveRejected(int32 CurTileX, int32 CurTileY)
	{
		// 서버가 거절했으므로 현재 위치를 서버가 준 값으로 강제 동기화하고 입력 락만 푼다.
		TilePos = { static_cast<float>(CurTileX), static_cast<float>(CurTileY) };
		TargetPos = TilePos * static_cast<float>(TileSize);
		X = TargetPos.X;
		Y = TargetPos.Y;
		bIsMoving = false;
		bIsPending = false;
	}
}