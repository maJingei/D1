#pragma once

#include <atomic>
#include <iostream>
#include <string>

#include "Core/CoreMinimal.h"
#include "Iocp/PacketSession.h"

/** 세션 로그인 상태. DB 워커가 로그인 확정 후 Playing 전환, OnDisconnected 에서 save 여부 판단. */
enum class ESessionState : uint8
{
	NotLoggedIn = 0,
	Playing     = 1,
};

/** 서버 측 세션: PacketSession 을 상속하여 OnRecvPacket 을 서버 핸들러 테이블로 디스패치한다. */
class GameServerSession : public PacketSession
{
public:
	~GameServerSession()
	{
		std::cout << "GameServerSession::~GameServerSession()" << "\n";
	}

	uint64 GetPlayerID() const { return PlayerID; }
	void SetPlayerID(uint64 InPlayerID) { PlayerID = InPlayerID; }

	int32 GetLevelID() const { return LevelID; }
	void SetLevelID(int32 InLevelID) { LevelID = InLevelID; }

	ESessionState GetState() const { return State.load(); }
	void SetState(ESessionState InState) { State.store(InState); }

	const std::string& GetAccountId() const { return AccountId; }
	/** DB 워커가 로그인 확정 시 1회 기록. proto string 그대로 복사. */
	void SetAccountId(const std::string& InId) { AccountId = InId; }

	/** 송신 hook — 봇 테스트 가시성을 위해 패킷명/Player/Level 을 stdout 에 1줄 로깅 후 base 호출. */
	void Send(SendBufferRef InSendBuffer) override;

protected:
	void OnRecvPacket(BYTE* Buffer, int32 Len) override;
	void OnDisconnected() override;

private:
	uint64 PlayerID = 0;
	int32 LevelID = -1;

	/** dbo.Account.Id 사본. Logout 시 World.AccountMap 에서 본인 제거에 사용. */
	std::string AccountId;

	/** IOCP↔DB 워커 공유 상태. atomic 으로 가시성/순서성 보장(seq_cst 기본). */
	std::atomic<ESessionState> State{ESessionState::NotLoggedIn};
};