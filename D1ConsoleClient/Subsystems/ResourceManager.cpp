#include "ResourceManager.h"
#include "Texture.h"
#include "../Game/World/UCollisionMap.h"

#include <gdiplus.h>

namespace D1
{
	/*-----------------------------------------------------------------*/
	/*  리소스 목록 — 새 리소스 추가 시 이 배열에만 항목을 추가한다.   */
	/*-----------------------------------------------------------------*/

	const FTextureEntry ResourceManager::TextureEntries[] =
	{
		{ L"ArenaTileset", L"../Resource/Arena Tileset.png" },
		{ L"PlayerSprite", L"../Resource/Adventurer Sprite Sheet v1.6.png" },
	};
	const int32 ResourceManager::TextureEntryCount = static_cast<int32>(std::size(TextureEntries));

	// 렌더링 순서: Ground → Water -> Wall 
	const FTileLayerEntry ResourceManager::TileLayerEntries[] =
	{
		{ L"../Resource/test_Render_Ground.csv", L"ArenaTileset" },
        { L"../Resource/test_Render_Water.csv",  L"ArenaTileset" },
		{ L"../Resource/test_Render_Wall.csv",   L"ArenaTileset" },
	};
	const int32 ResourceManager::TileLayerEntryCount = static_cast<int32>(std::size(TileLayerEntries));

	// 충돌 전용 CSV (0=통행, 1=차단). 렌더 레이어와 독립적으로 1회 로드.
	const wchar_t* const ResourceManager::CollisionMapPath = L"../Resource/Collision_Collision.csv";

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

	std::shared_ptr<Texture> ResourceManager::Load(const std::wstring& Name, const std::wstring& Path)
	{
		// 이미 등록된 이름이면 캐시에서 반환
		auto It = TextureCache.find(Name);
		if (It != TextureCache.end())
			return It->second;

		// 새로 로드 후 이름으로 등록
		auto NewTexture = std::make_shared<Texture>();
		if (!NewTexture->Load(Path))
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
		if (!Map->Load(CollisionMapPath))
			return nullptr;
		return Map;
	}
}