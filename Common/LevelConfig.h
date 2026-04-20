#pragma once

#include "Core/CoreMinimal.h"

/**
 * 서버/클라이언트가 공유하는 Level 스케일 및 폴더명 상수.
 * 활성 Level 수(LEVEL_COUNT)만 확장하면 기존에 등록된 폴더가 바로 사용된다.
 */

/** 현재 활성화된(포탈로 연결된) Level 개수. World/UWorld 고정 배열 크기이자 유효 LevelID 범위 [0, LEVEL_COUNT). */
constexpr int32 LEVEL_COUNT = 10;

/** 등록된 Level Resource 하위 폴더명 전체 목록. 인덱스 = LevelID. 배포 패키지에 10개 폴더 모두 포함. */
constexpr const char* LevelFolders[] =
{
	"Level01",
	"Level02",
	"Level03",
	"Level04",
	"Level05",
	"Level06",
	"Level07",
	"Level08",
	"Level09",
	"Level10",
};

/** 등록된 폴더 총 개수. LEVEL_COUNT 는 이 값을 넘을 수 없다. */
constexpr int32 AVAILABLE_LEVEL_COUNT = static_cast<int32>(sizeof(LevelFolders) / sizeof(LevelFolders[0]));

static_assert(LEVEL_COUNT > 0, "LEVEL_COUNT must be positive");
static_assert(LEVEL_COUNT <= AVAILABLE_LEVEL_COUNT, "LEVEL_COUNT cannot exceed registered LevelFolders entries");