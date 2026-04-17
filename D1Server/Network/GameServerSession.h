#pragma once

#include "Core/CoreMinimal.h"
#include "Iocp/PacketSession.h"

/** 서버 측 세션: PacketSession 을 상속하여 OnRecvPacket 을 서버 핸들러 테이블로 디스패치한다. */
class GameServerSession : public PacketSession
{
public:
	uint64 GetPlayerID() const { return PlayerID; }
	void SetPlayerID(uint64 InPlayerID) { PlayerID = InPlayerID; }

	int32 GetLevelID() const { return LevelID; }
	void SetLevelID(int32 InLevelID) { LevelID = InLevelID; }

protected:
	void OnRecvPacket(BYTE* Buffer, int32 Len) override;
	void OnDisconnected() override;

private:
	/** C_ENTER_GAME 처리로 0 이 아닌 값이 들어간다. 0 은 '아직 입장 전' 상태. */
	uint64 PlayerID = 0;

	/** C_ENTER_GAME 처리 시 World 에서 할당받은 Level 인덱스. -1 은 '아직 입장 전'. */
	int32 LevelID = -1;
};