#include "ResourceManager.h"
#include "Texture.h"
#include "UCollisionMap.h"
#include "LevelConfig.h"

#include <gdiplus.h>

/*-----------------------------------------------------------------*/
/*  리소스 목록 — 새 리소스 추가 시 이 배열에만 항목을 추가한다.   */
/*  경로는 exe 디렉토리 기준 상대 경로 (배포 시 exe 옆 Resource/). */
/*-----------------------------------------------------------------*/

const FTextureEntry ResourceManager::TextureEntries[] =
{
	{ L"ArenaTileset", L"Resource/Arena Tileset.png" },
	// 기존 row-기반 단일 시트 PlayerSprite — DefaultConfig 가 Directional 로 전환되면서 더 이상 사용되지 않으나
	// Female/Dwarf 가 같은 류의 단일 시트 모드를 유지하므로 비교/롤백을 위해 엔트리는 보존(주석 처리 대신 유지).
	{ L"PlayerSprite", L"Resource/Adventurer Sprite Sheet v1.6.png" },
	// { L"PlayerFemaleSprite", L"Resource/Adventurer Female Sprite Sheet.png" },
	{ L"PlayerDwarfSprite", L"Resource/Gladiator-Sprite Sheet.png" },
	{ L"MiniGolemSprite", L"Resource/Mini Golem Sprite Sheet.png" },
	{ L"HitEffect", L"Resource/Hit.bmp" },
	{ L"HealthBarSheet", L"Resource/HealthBar-Sheet.png" },

	// PlayerSprite 4방향 멀티파일 시트 — 신규 DefaultConfig 가 사용. 상태×방향=12장, 96×80, 1행 8프레임.
	{ L"PlayerIdle_Down",    L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/IDLE/idle_down.png" },
	{ L"PlayerIdle_Left",    L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/IDLE/idle_left.png" },
	{ L"PlayerIdle_Right",   L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/IDLE/idle_right.png" },
	{ L"PlayerIdle_Up",      L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/IDLE/idle_up.png" },
	{ L"PlayerWalk_Down",    L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/RUN/run_down.png" },
	{ L"PlayerWalk_Left",    L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/RUN/run_left.png" },
	{ L"PlayerWalk_Right",   L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/RUN/run_right.png" },
	{ L"PlayerWalk_Up",      L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/RUN/run_up.png" },
	{ L"PlayerAttack_Down",  L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/ATTACK 1/attack1_down.png" },
	{ L"PlayerAttack_Left",  L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/ATTACK 1/attack1_left.png" },
	{ L"PlayerAttack_Right", L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/ATTACK 1/attack1_right.png" },
	{ L"PlayerAttack_Up",    L"Resource/FREE_Adventurer 2D Pixel Art/Sprites/ATTACK 1/attack1_up.png" },

	// Samurai (Mirror 모드) — 상태별 1장, 96×96 프레임, 1행 N프레임. 좌향은 bFacingLeft 로 GDI 미러링.
	{ L"PlayerSamuraiIdle",   L"Resource/FREE_Samurai 2D Pixel Art v1.2/Sprites/IDLE.png" },
	{ L"PlayerSamuraiWalk",   L"Resource/FREE_Samurai 2D Pixel Art v1.2/Sprites/RUN.png" },
	{ L"PlayerSamuraiAttack", L"Resource/FREE_Samurai 2D Pixel Art v1.2/Sprites/ATTACK 1.png" },
};
const int32 ResourceManager::TextureEntryCount = static_cast<int32>(std::size(TextureEntries));

// 렌더링 순서: Ground → Water -> Wall
// CsvPath 는 Level 폴더 내부 상대 경로. 최종 경로는 ResolveLevelPath(LevelID, ...) 로 조합.
const FTileLayerEntry ResourceManager::TileLayerEntries[] =
{
	{ L"test_Render_Ground.csv", L"ArenaTileset" },
	{ L"test_Render_Water.csv",  L"ArenaTileset" },
	{ L"test_Render_Wall.csv",   L"ArenaTileset" },
};
const int32 ResourceManager::TileLayerEntryCount = static_cast<int32>(std::size(TileLayerEntries));

// 마일스톤 1: 충돌 맵 파일명은 LevelConfig.h::LevelCollisionFiles[LevelID] 로 이전.
// 기존 단일 상수는 LoadCollisionMap 호출 경로 변경으로 더 이상 사용되지 않는다.
// const wchar_t* const ResourceManager::CollisionMapPath = L"Collision_Collision.csv";

/*-----------------------------------------------------------------*/

ResourceManager& ResourceManager::Get()
{
	// Meyers singleton: dtor가 자동 호출되어 캐시/GdiplusToken을 해제한다.
	static ResourceManager Instance;
	return Instance;
}

ResourceManager::~ResourceManager()
{
	// Shutdown은 멱등이므로 명시적 Shutdown 호출 후에도 안전.
	Shutdown();
}

void ResourceManager::Initialize()
{
	// 1. exe 디렉토리를 BaseDir 로 캐싱.
	//    PostBuildEvent 가 "$(OutDir)Resource" 로 리소스 폴더를 복사하므로
	//    배포된 환경에서도 exe 옆에 Resource\ 가 함께 존재한다.
	//    엔트리 경로 "Resource/..." 와 결합하면 exe 기준 절대 경로가 된다.
	wchar_t ExePath[MAX_PATH] = { 0 };
	const DWORD Len = ::GetModuleFileNameW(nullptr, ExePath, MAX_PATH);
	if (Len > 0 && Len < MAX_PATH)
	{
		std::wstring Path(ExePath);
		const size_t LastSep = Path.find_last_of(L"\\/");
		if (LastSep != std::wstring::npos)
			BaseDir = Path.substr(0, LastSep + 1);
	}

	// 2. GDI+ 런타임 초기화 — 이미지 디코딩에 필요.
	Gdiplus::GdiplusStartupInput StartupInput;
	Gdiplus::GdiplusStartup(&GdiplusToken, &StartupInput, nullptr);
}

void ResourceManager::Shutdown()
{
	TextureCache.clear();
	if (GdiplusToken != 0)
	{
		Gdiplus::GdiplusShutdown(GdiplusToken);
		GdiplusToken = 0;
	}
}

std::wstring ResourceManager::ResolvePath(const std::wstring& RelativePath) const
{
	// BaseDir 가 비어 있으면(Initialize 이전) 안전하게 원본 그대로 반환.
	if (BaseDir.empty())
		return RelativePath;
	return BaseDir + RelativePath;
}

std::wstring ResourceManager::ResolveLevelPath(int32 LevelID, const std::wstring& LevelRelativePath) const
{
	// Level 폴더 내부 상대 경로를 Resource/{LevelFolders[LevelID]}/ 아래 절대 경로로 변환.
	// LevelID 범위 방어: 벗어나면 Level0 으로 폴백.
	const int32 SafeID = (LevelID >= 0 && LevelID < AVAILABLE_LEVEL_COUNT) ? LevelID : 0;
	const char* Folder = LevelFolders[SafeID];

	// ASCII 폴더명을 wchar_t 로 변환 (Level01 같은 ASCII 전용이므로 단순 복사로 충분).
	std::wstring WideFolder;
	for (const char* P = Folder; *P != '\0'; ++P)
		WideFolder.push_back(static_cast<wchar_t>(*P));

	std::wstring Combined = L"Resource/";
	Combined += WideFolder;
	Combined += L'/';
	Combined += LevelRelativePath;
	return ResolvePath(Combined);
}

std::shared_ptr<Texture> ResourceManager::Load(const std::wstring& Name, const std::wstring& Path)
{
	// 이미 등록된 이름이면 캐시에서 반환
	auto It = TextureCache.find(Name);
	if (It != TextureCache.end())
		return It->second;

	// 새로 로드 후 이름으로 등록 — 경로는 exe 기준 절대 경로로 변환해 전달
	auto NewTexture = std::make_shared<Texture>();
	if (!NewTexture->Load(ResolvePath(Path)))
		return nullptr;

	TextureCache[Name] = NewTexture;
	return NewTexture;
}

std::shared_ptr<Texture> ResourceManager::GetTexture(const std::wstring& Name)
{
	auto It = TextureCache.find(Name);
	if (It != TextureCache.end())
		return It->second;
	return nullptr;
}

std::shared_ptr<UCollisionMap> ResourceManager::LoadCollisionMap(int32 LevelID)
{
	// LevelCollisionFiles 인덱스 안전성 — 범위 밖이면 0 으로 폴백.
	const int32 SafeLevelID = (LevelID >= 0 && LevelID < AVAILABLE_LEVEL_COUNT) ? LevelID : 0;
	const char* CollisionFile = LevelCollisionFiles[SafeLevelID];

	// ASCII 파일명을 wchar_t 로 변환 (Level 자산 파일명은 ASCII 전용이므로 단순 복사).
	std::wstring WideCollisionFile;
	for (const char* Ptr = CollisionFile; *Ptr != '\0'; ++Ptr)
	{
		WideCollisionFile.push_back(static_cast<wchar_t>(*Ptr));
	}

	auto Map = std::make_shared<UCollisionMap>();
	if (!Map->Load(ResolveLevelPath(LevelID, WideCollisionFile)))
	{
		return nullptr;
	}
	return Map;
}

std::shared_ptr<Texture> ResourceManager::LoadLevelBackground(int32 LevelID)
{
	// LevelBackgroundImages 인덱스 안전성 — 범위 밖이면 0 으로 폴백.
	const int32 SafeLevelID = (LevelID >= 0 && LevelID < AVAILABLE_LEVEL_COUNT) ? LevelID : 0;

	// 마일스톤 2: 모든 Level 이 단일 이미지 모드로 통일되어 LevelUseSingleImage 체크는 제거됨.
	const char* BackgroundFile = LevelBackgroundImages[SafeLevelID];
	if (BackgroundFile == nullptr || BackgroundFile[0] == '\0')
	{
		return nullptr;
	}

	// 캐시 이름: 같은 이름의 PNG 가 다른 Level 에서 우연히 겹치지 않도록 LevelID 를 접미.
	const std::wstring CacheName = L"LevelBackground_" + std::to_wstring(SafeLevelID);
	auto It = TextureCache.find(CacheName);
	if (It != TextureCache.end())
	{
		return It->second;
	}

	// ASCII 파일명을 wchar_t 로 변환.
	std::wstring WideBackgroundFile;
	for (const char* Ptr = BackgroundFile; *Ptr != '\0'; ++Ptr)
	{
		WideBackgroundFile.push_back(static_cast<wchar_t>(*Ptr));
	}

	auto NewTexture = std::make_shared<Texture>();
	if (!NewTexture->Load(ResolveLevelPath(SafeLevelID, WideBackgroundFile)))
	{
		return nullptr;
	}

	TextureCache[CacheName] = NewTexture;
	return NewTexture;
}
