#pragma once

#include "Core/CoreMinimal.h"
#include <vector>
#include <memory>
#include <string>

#include "Core/CoreMinimal.h"

namespace Gdiplus { class Bitmap; }

class Texture;

/** CSV 타일맵 데이터와 타일셋 Texture를 보유하고 배경으로 렌더링한다. */
class UTileMap
{
public:
	UTileMap();
	~UTileMap();

	/** CSV 타일맵과 타일셋 Texture를 로드한다. */
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
