#include "ResourceManager.h"
#include "Texture.h"

#include <gdiplus.h>

namespace D1
{
	/*-----------------------------------------------------------------*/
	/*  리소스 목록 — 새 리소스 추가 시 이 배열에만 항목을 추가한다.   */
	/*-----------------------------------------------------------------*/

	const FTextureEntry ResourceManager::TextureEntries[] =
	{
		{ L"ArenaTileset", L"../Resource/Arena Tileset.png" },
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

	/*-----------------------------------------------------------------*/

	ResourceManager& ResourceManager::Get()
	{
		static ResourceManager* Instance = new ResourceManager();
		return *Instance;
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
}