#include "ResourceManager.h"
#include "Texture.h"
#include "UCollisionMap.h"

#include <gdiplus.h>

/*-----------------------------------------------------------------*/
/*  리소스 목록 — 새 리소스 추가 시 이 배열에만 항목을 추가한다.   */
/*  경로는 exe 디렉토리 기준 상대 경로 (배포 시 exe 옆 Resource/). */
/*-----------------------------------------------------------------*/

const FTextureEntry ResourceManager::TextureEntries[] =
{
	{ L"ArenaTileset",    L"Resource/Arena Tileset.png" },
	{ L"PlayerSprite",    L"Resource/Adventurer Sprite Sheet v1.6.png" },
	{ L"MiniGolemSprite", L"Resource/Mini Golem Sprite Sheet.png" },
	{ L"HitEffect",       L"Resource/Hit.bmp" },
	{ L"HealthBarSheet",  L"Resource/HealthBar-Sheet.png" },
};
const int32 ResourceManager::TextureEntryCount = static_cast<int32>(std::size(TextureEntries));

// 렌더링 순서: Ground → Water -> Wall
const FTileLayerEntry ResourceManager::TileLayerEntries[] =
{
	{ L"Resource/test_Render_Ground.csv", L"ArenaTileset" },
	{ L"Resource/test_Render_Water.csv",  L"ArenaTileset" },
	{ L"Resource/test_Render_Wall.csv",   L"ArenaTileset" },
};
const int32 ResourceManager::TileLayerEntryCount = static_cast<int32>(std::size(TileLayerEntries));

// 충돌 전용 CSV (0=통행, 1=차단). 렌더 레이어와 독립적으로 1회 로드.
const wchar_t* const ResourceManager::CollisionMapPath = L"Resource/Collision_Collision.csv";

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

std::shared_ptr<UCollisionMap> ResourceManager::LoadCollisionMap()
{
	auto Map = std::make_shared<UCollisionMap>();
	if (!Map->Load(ResolvePath(CollisionMapPath)))
		return nullptr;
	return Map;
}
