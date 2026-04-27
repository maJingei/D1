#pragma once

#include "Core/CoreMinimal.h"
#include "Protocol.pb.h"

/**
 * 서버/클라이언트가 공유하는 Level 스케일 및 폴더명 상수.
 * 활성 Level 수(LEVEL_COUNT)만 확장하면 기존에 등록된 폴더가 바로 사용된다.
 */

/** 현재 활성화된(포탈로 연결된) Level 개수. World/UWorld 고정 배열 크기이자 유효 LevelID 범위 [0, LEVEL_COUNT). */
constexpr int32 LEVEL_COUNT = 2;

/** 등록된 Level Resource 하위 폴더명 전체 목록. 인덱스 = LevelID. 배포 패키지에 두 폴더 모두 포함. */
constexpr const char* LevelFolders[] =
{
	"NewLevel1",
	"NewLevel2",
};

/**
 * 각 Level 폴더 내부의 충돌 맵 CSV 파일명. 인덱스 = LevelID. 0=walkable, 1=blocked.
 * NewLevel 자산은 폴더별 고유 파일명을 가지므로 폴더-파일명을 분리해 보관한다.
 */
constexpr const char* LevelCollisionFiles[] =
{
	"Level1Collision.csv",
	"Level2Collision.csv",
};

/**
 * 단일 이미지 렌더 모드에서 사용하는 배경 PNG 파일명 (Level 폴더 내부 상대 경로).
 * 마일스톤 2 이후 모든 Level 이 단일 이미지 모드로 통일되어 전용 분기 플래그(LevelUseSingleImage)는 제거되었다.
 */
constexpr const char* LevelBackgroundImages[] =
{
	"Level1.png",
	"Level2.png",
};

/** 등록된 폴더 총 개수. LEVEL_COUNT 는 이 값을 넘을 수 없다. */
constexpr int32 AVAILABLE_LEVEL_COUNT = static_cast<int32>(sizeof(LevelFolders) / sizeof(LevelFolders[0]));

static_assert(LEVEL_COUNT > 0, "LEVEL_COUNT must be positive");
static_assert(LEVEL_COUNT <= AVAILABLE_LEVEL_COUNT, "LEVEL_COUNT cannot exceed registered LevelFolders entries");
static_assert(sizeof(LevelCollisionFiles) / sizeof(LevelCollisionFiles[0]) == AVAILABLE_LEVEL_COUNT, "LevelCollisionFiles size must match LevelFolders");
static_assert(sizeof(LevelBackgroundImages) / sizeof(LevelBackgroundImages[0]) == AVAILABLE_LEVEL_COUNT, "LevelBackgroundImages size must match LevelFolders");

/** 한 마리 몬스터의 스폰 정의. Level 별 정적 배열에 모아 Level::Init 이 순회 스폰한다. */
struct MonsterSpawnInfo
{
	Protocol::MonsterType Type;
	int32 TileX;
	int32 TileY;
};

/** Level1 몬스터 스폰 — 슬라임 1마리. 기존 (10,10) 좌표 유지(회귀 방지). */
constexpr MonsterSpawnInfo Level1Monsters[] =
{
	{ Protocol::MT_SLIME, 10, 10 },
};

/** Level 인덱스로 스폰 테이블 포인터/개수를 조회. LEVEL_COUNT 확장 시 슬롯 추가만으로 끝난다. */
struct MonsterSpawnTable
{
	const MonsterSpawnInfo* Entries;
	int32 Count;
};

/** Level2 는 현재 몬스터 없음 — Entries=nullptr/Count=0 으로 Level::Init 의 0회 루프 처리에 위임. */
constexpr MonsterSpawnTable LevelMonsterSpawns[] =
{
	{ Level1Monsters, static_cast<int32>(sizeof(Level1Monsters) / sizeof(Level1Monsters[0])) },
	{ nullptr, 0 },
};

static_assert(sizeof(LevelMonsterSpawns) / sizeof(LevelMonsterSpawns[0]) == AVAILABLE_LEVEL_COUNT, "LevelMonsterSpawns size must match LevelFolders");