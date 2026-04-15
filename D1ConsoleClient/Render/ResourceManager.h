#pragma once

#include <Windows.h>
#include <map>
#include <memory>
#include <string>

#include "Core/Types.h"

namespace D1
{
	class Texture;
	class UCollisionMap;

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

		/** 충돌 맵 CSV 경로 (0=통행, 1=차단). 렌더링하지 않는 논리 전용 레이어. */
		static const wchar_t* const CollisionMapPath;

	public:
		static ResourceManager& Get();

		/**
		 * 실행 파일 기준 상대 경로를 절대 경로로 변환한다.
		 * 배포 시 exe 옆에 Resource/ 폴더를 두고 어디서 실행하든 동일하게 참조되도록 한다.
		 *
		 * @param RelativePath  exe 디렉토리 기준 상대 경로 (예: L"Resource/Foo.png")
		 * @return              exe 디렉토리 prefix 가 붙은 절대 경로
		 */
		std::wstring ResolvePath(const std::wstring& RelativePath) const;

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

		/**
		 * 충돌 맵 CSV(CollisionMapPath)를 로드하여 UCollisionMap 인스턴스를 생성한다.
		 * 캐싱하지 않으며, 호출자가 소유권을 받아 UWorld 등에 주입한다.
		 *
		 * @return  로드 성공 시 UCollisionMap, 실패 시 nullptr.
		 */
		std::shared_ptr<UCollisionMap> LoadCollisionMap();

	private:
		ResourceManager() = default;
		~ResourceManager();
		ResourceManager(const ResourceManager&) = delete;
		ResourceManager& operator=(const ResourceManager&) = delete;

		std::map<std::wstring, std::shared_ptr<Texture>> TextureCache;
		ULONG_PTR GdiplusToken = 0;

		/** 실행 파일이 위치한 디렉토리 (끝에 '\' 포함). Initialize() 에서 1회 결정되어 ResolvePath() prefix 로 쓰인다. */
		std::wstring BaseDir;
	};
}