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

/** 플레이어가 조종하는 액터. */
class APlayerActor : public ACharacterActor
{
public:
	APlayerActor(uint64 InPlayerID, int32 SpawnTileX, int32 SpawnTileY);
	virtual ~APlayerActor();

	virtual void Tick(float DeltaTime) override;
	virtual void Render(HDC BackDC) override;

	uint64 GetPlayerID() const { return PlayerID; }

	/** 이 액터가 로컬 클라이언트의 조작 대상인지 반환한다. */
	bool IsLocallyControlled() const;

	/** 서버가 이동을 승인했을 때(S_MOVE 수신) 호출한다. */
	void OnServerMoveAccepted(int32 NextTileX, int32 NextTileY, Protocol::Direction Dir, uint64 ClientSeq);

	/** 서버가 이동을 거절했을 때(S_MOVE_REJECT 수신) 호출한다. */
	void OnServerMoveRejected(uint64 LastAcceptedSeq, int32 ServerTileX, int32 ServerTileY);

	/** 체력바 갱신 + 베이스의 Hit 이펙트 스폰 훅 호출. */
	virtual void OnServerDamaged(int32 InHP, int32 InMaxHP) override;

protected:
	virtual float GetAttackDuration() const override { return AttackDuration; }

private:
	void ProcessInput();
	void HandleAttackInput();
	void HandleMoveInput();

	/** 서버가 부여한 플레이어 고유 ID. */
	uint64 PlayerID = 0;

	/** C_MOVE 에 실어 보낼 단조 증가 시퀀스. 0 은 "아직 이동하지 않음" 을 의미하도록 1 부터 시작. */
	uint64 NextClientSeq = 1;

	/** 예측 스냅 샷 저장 배열 */
	std::vector<FMoveSnapshot> SnapshotQueue;

	/** 플레이어 머리 위에 렌더되는 체력바. APlayerActor 가 소유(World Actor 아님). */
	std::unique_ptr<UHealthBarWidget> HealthBar;

	// ---------------------------------------------------------------
	// 스프라이트 시트 레이아웃 (Adventurer Sprite Sheet v1.6, 32×32 프레임)
	// ---------------------------------------------------------------

	static constexpr FSpriteClipInfo IdleClip   = { 0, 13, 8.f };
	static constexpr FSpriteClipInfo WalkClip   = { 1,  8, 25.f };
	static constexpr FSpriteClipInfo AttackClip = { 3, 10, 15.f };

	/** 공격 1사이클 재생 시간(초). Sprite 가 자동 루프하므로 이 시간만큼 지나면 bIsAttacking 해제. */
	static constexpr float AttackDuration = static_cast<float>(AttackClip.Frames) / AttackClip.Fps;

	/** 픽셀/초 — 생성자에서 ACharacterActor::MoveSpeed 에 덮어쓴다. */
	static constexpr float PlayerMoveSpeed = 260.f;
};