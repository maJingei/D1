#pragma once

#include <Windows.h>
#include <map>
#include <memory>
#include <string>

#include "Core/Types.h"

namespace D1
{
	class Texture;

	/** 텍스처 등록 항목. 이름-파일경로 쌍. */
	struct FTextureEntry
	{
		const wchar_t* Name;
		const wchar_t* Path;
	};

	/** 타일 레이어 항목. CSV 경로와 사용할 타일셋 이름. */
	struct FTileLayerEntry
	{
		const wchar_t* CsvPath;
		const wchar_t* TilesetName;
	};

	/**
	 * 이미지 파일을 이름-Texture 쌍으로 캐싱하는 싱글톤.
	 *
	 * 리소스 목록은 ResourceManager.cpp 의 정적 배열(TextureEntries, TileLayerEntries)에만 추가한다.
	 * Game::LoadResources() 는 이 배열을 순회하므로 다른 파일을 수정할 필요 없다.
	 */
	class ResourceManager
	{
	public:
		/** 로드할 텍스처 목록 (ResourceManager.cpp 에 정의). */
		static const FTextureEntry TextureEntries[];
		static const int32 TextureEntryCount;

		/** 렌더링할 타일 레이어 목록, 순서대로 렌더링됨 (ResourceManager.cpp 에 정의). */
		static const FTileLayerEntry TileLayerEntries[];
		static const int32 TileLayerEntryCount;

	public:
		static ResourceManager& Get();

		/** GDI+ 를 초기화한다. Game::Initialize() 에서 호출한다. */
		void Initialize();

		/** GDI+ 를 종료하고 캐시를 비운다. Game::Shutdown() 에서 호출한다. */
		void Shutdown();

		/**
		 * 이미지를 로드하고 지정한 이름으로 등록한다.
		 * 이미 같은 이름이 등록되어 있으면 기존 항목을 반환한다.
		 *
		 * @param Name  이후 GetTexture() 에서 사용할 식별 이름
		 * @param Path  이미지 파일 경로
		 * @return      로드된 Texture 의 shared_ptr. 실패 시 nullptr.
		 */
		std::shared_ptr<Texture> Load(const std::wstring& Name, const std::wstring& Path);

		/**
		 * Load() 로 등록한 이름으로 Texture 를 조회한다.
		 *
		 * @param Name  Load() 시 지정한 식별 이름
		 * @return      등록된 Texture 의 shared_ptr. 없으면 nullptr.
		 */
		std::shared_ptr<Texture> GetTexture(const std::wstring& Name);

	private:
		ResourceManager() = default;
		~ResourceManager() = default;
		ResourceManager(const ResourceManager&) = delete;
		ResourceManager& operator=(const ResourceManager&) = delete;

		std::map<std::wstring, std::shared_ptr<Texture>> TextureCache;
		ULONG_PTR GdiplusToken = 0;
	};
}