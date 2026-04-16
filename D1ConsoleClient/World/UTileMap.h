#pragma once

#include "Core/CoreMinimal.h"
#include <vector>
#include <memory>
#include <string>

#include "Core/CoreMinimal.h"

namespace Gdiplus { class Bitmap; }

class Texture;

/**
 * CSV 타일맵 데이터와 타일셋 Texture를 보유하고 배경으로 렌더링한다.
 * 최초 Render 호출 시 타일들을 캐시 비트맵에 한 번만 합성하고,
 * 이후 호출에서는 캐시된 비트맵을 통째로 출력한다 (고정 화면 가정).
 */
class UTileMap
{
public:
	UTileMap();
	~UTileMap();

	/**
	 * CSV 타일맵과 타일셋 Texture를 로드한다.
	 *
	 * @param CsvPath        타일 인덱스 CSV 파일 경로
	 * @param TilesetName    ResourceManager에 등록된 타일셋 이름
	 * @param InTileColumns  타일셋 가로 열 수 (기본 16)
	 * @param InTileSize     타일 한 칸 크기(px) (기본 32)
	 * @return               로드 성공 여부
	 */
	bool Load(const std::wstring& CsvPath, const std::wstring& TilesetName, int32 InTileColumns = 16, int32 InTileSize = 32);

	/** 타일맵을 BackDC에 렌더링한다. UWorld::Render에서 Actor 이전에 호출한다. */
	void Render(HDC BackDC);

private:
	/** 캐시 비트맵을 생성하고 모든 타일을 1회 합성한다. */
	void BuildCache();

	std::shared_ptr<Texture> TilesetTexture;
	std::vector<std::vector<int32>> TileIndices; // [행][열], -1=빈 칸, 0+=0-based 타일 ID
	int32 TileColumns = 16;
	int32 TileSize = 32;

	/** 타일맵 전체를 1회 합성해둔 캐시. nullptr면 다음 Render에서 생성. */
	std::unique_ptr<Gdiplus::Bitmap> CachedMap;
};
