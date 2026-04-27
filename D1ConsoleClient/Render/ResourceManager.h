#pragma once

#include "Core/CoreMinimal.h"
#include <map>
#include <memory>
#include <string>

#include "Core/CoreMinimal.h"

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

/** 이미지 파일을 이름-Texture 쌍으로 캐싱하는 싱글톤. */
class ResourceManager
{
public:
	/** 로드할 텍스처 목록 (ResourceManager.cpp 에 정의). */
	static const FTextureEntry TextureEntries[];
	static const int32 TextureEntryCount;

	/** 렌더링할 타일 레이어 목록, 순서대로 렌더링됨 (ResourceManager.cpp 에 정의). */
	static const FTileLayerEntry TileLayerEntries[];
	static const int32 TileLayerEntryCount;

	// 마일스톤 1: 충돌 맵 CSV 파일명은 LevelConfig.h::LevelCollisionFiles[LevelID] 로 이전됨.
	// 기존 단일 상수는 호출 지점이 ResolveLevelPath + LevelCollisionFiles 조합으로 대체되어 더 이상 필요하지 않다.
	// /** 충돌 맵 CSV 경로 (0=통행, 1=차단). 렌더링하지 않는 논리 전용 레이어. */
	// static const wchar_t* const CollisionMapPath;

public:
	static ResourceManager& Get();

	/** 실행 파일 기준 상대 경로를 절대 경로로 변환한다. */
	std::wstring ResolvePath(const std::wstring& RelativePath) const;

	/** 지정 LevelID 의 Level 폴더(Resource/LevelFolders[LevelID]/) 내부 상대 경로를 절대 경로로 변환한다. */
	std::wstring ResolveLevelPath(int32 LevelID, const std::wstring& LevelRelativePath) const;

	/** GDI+ 를 초기화한다. Game::Initialize() 에서 호출한다. */
	void Initialize();

	/** GDI+ 를 종료하고 캐시를 비운다. Game::Shutdown() 에서 호출한다. */
	void Shutdown();

	/** 이미지를 로드하고 지정한 이름으로 등록한다. */
	std::shared_ptr<Texture> Load(const std::wstring& Name, const std::wstring& Path);

	/** Load() 로 등록한 이름으로 Texture 를 조회한다. */
	std::shared_ptr<Texture> GetTexture(const std::wstring& Name);

	/** 지정 LevelID 의 충돌 맵 CSV 를 로드하여 UCollisionMap 인스턴스를 생성한다. */
	std::shared_ptr<UCollisionMap> LoadCollisionMap(int32 LevelID);

	/**
	 * 마일스톤 1: 지정 LevelID 의 단일 배경 이미지 PNG 를 로드하여 Texture 로 캐싱한다.
	 * LevelUseSingleImage[LevelID] == false 거나 LevelBackgroundImages[LevelID] 가 비어 있으면 nullptr 을 반환한다.
	 */
	std::shared_ptr<Texture> LoadLevelBackground(int32 LevelID);

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
