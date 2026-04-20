#pragma once

#include "Core/Types.h"

namespace D1LoadBot
{
	/**
	 * 봇이 직접 사용하는 패킷 ID 목록.
	 *
	 * ClientPacketHandler.h(서버) / ServerPacketHandler.h(콘솔 클라) 와 수치 규약을 동일하게 유지한다.
	 * (PacketID 는 1000 부터 1 씩 증가 — 프로젝트 전역 규약)
	 *
	 * Handler 파일별로 ID 를 정의한다는 프로젝트 관행을 따르기 위해 봇 전용 헤더로 분리했다.
	 */
	enum : uint16
	{
		PKT_C_LOGIN = 1000,
		PKT_S_LOGIN = 1001,
		PKT_C_ENTER_GAME = 1002,
		PKT_S_ENTER_GAME = 1003,
		PKT_S_SPAWN = 1004,
		PKT_C_MOVE = 1005,
		PKT_S_MOVE = 1006,
		PKT_S_MOVE_REJECT = 1007,
	};
}