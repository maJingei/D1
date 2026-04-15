#pragma once

#include <atomic>
#include "Core/Types.h"

/**
 * 부하 벤치마크용 C_MOVE 수신 카운터.
 *
 * ClientPacketHandler.h 는 GenPacket 도구에 의해 재생성되므로 여기에 선언을 분리했다.
 * Handle_C_MOVE 진입 시 증가하고, D1Server.cpp 의 TPS 로거가 1초 주기로 diff 를 찍는다.
 */
extern std::atomic<uint64> GMoveCounter;
