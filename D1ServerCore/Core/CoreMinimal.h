#pragma once

/** 프로젝트 전역에서 사용하는 최소 공통 헤더 묶음. */

#ifndef NOMINMAX
#define NOMINMAX
#endif

// winsock2 는 windows.h 보다 먼저 포함되어야 구형 winsock.h 가 딸려 들어오지 않는다.
// (WIN32_LEAN_AND_MEAN 정의하지 않음 — D1ConsoleClient 의 GdiPlus 가 풀 GDI 를 필요로 한다.)
#include <winsock2.h>
#include <windows.h>

// 자주 쓰는 STL
#include <atomic>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// D1 공용 타입 / 스마트 포인터 alias
#include "Types.h"