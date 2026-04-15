#include "UTileMap.h"
#include "Render/Texture.h"
#include "Render/ResourceManager.h"

#include <gdiplus.h>

#include <fstream>
#include <sstream>
#include <string>

namespace D1
{
	UTileMap::UTileMap() = default;
	UTileMap::~UTileMap() = default;

	bool UTileMap::Load(const std::wstring& CsvPath, const std::wstring& TilesetName, int32 InTileColumns, int32 InTileSize)
	{
		TileColumns = InTileColumns;
		TileSize = InTileSize;

		// ResourceManager에서 이름으로 타일셋 Texture 조회
		TilesetTexture = ResourceManager::Get().GetTexture(TilesetName);
		if (!TilesetTexture)
			return false;

		// CSV 파싱: 쉼표 구분 정수 → TileIndices[행][열]
		std::ifstream File(CsvPath);
		if (!File.is_open())
			return false;

		TileIndices.clear();
		std::string Line;
		while (std::getline(File, Line))
		{
			if (Line.empty())
				continue;

			std::vector<int32> Row;
			std::istringstream Stream(Line);
			std::string Token;
			while (std::getline(Stream, Token, ','))
			{
				// 본 프로젝트 CSV 규약: -1 = 빈 타일, 0+ = 0-based 타일 ID
				Row.push_back(std::stoi(Token));
			}
			TileIndices.push_back(std::move(Row));
		}

		// CSV가 다시 로드되면 캐시도 무효화
		CachedMap.reset();
		return !TileIndices.empty();
	}

	void UTileMap::Render(HDC BackDC)
	{
		if (!TilesetTexture)
			return;

		// 최초 호출에서 한 번만 캐시를 만든다 — 이후 프레임은 그대로 재사용 (고정 화면 가정).
		if (!CachedMap)
			BuildCache();

		if (!CachedMap)
			return;

		// 캐시 비트맵을 백버퍼로 1회 출력. PARGB 캐시이므로 알파 보존.
		Gdiplus::Graphics G(BackDC);
		G.SetPageUnit(Gdiplus::UnitPixel);
		G.DrawImage(CachedMap.get(), 0, 0, static_cast<INT>(CachedMap->GetWidth()), static_cast<INT>(CachedMap->GetHeight()));
	}

	void UTileMap::BuildCache()
	{
		Gdiplus::Bitmap* Tileset = TilesetTexture->GetBitmap();
		if (!Tileset || TileIndices.empty())
			return;

		// 캐시 크기: 첫 행의 열 수를 맵 가로로 사용 (정형 격자 가정).
		const int32 MapCols = static_cast<int32>(TileIndices[0].size());
		const int32 MapRows = static_cast<int32>(TileIndices.size());
		const INT   PixelW  = static_cast<INT>(MapCols * TileSize);
		const INT   PixelH  = static_cast<INT>(MapRows * TileSize);

		// PARGB 포맷으로 알파를 보존하면서 합성. 초기 픽셀은 투명(0,0,0,0)이라 빈 타일은 비워진 상태가 된다.
		CachedMap = std::make_unique<Gdiplus::Bitmap>(PixelW, PixelH, PixelFormat32bppPARGB);

		Gdiplus::Graphics G(CachedMap.get());
		G.SetPageUnit(Gdiplus::UnitPixel);

		for (int32 Row = 0; Row < MapRows; Row++)
		{
			for (int32 Col = 0; Col < static_cast<int32>(TileIndices[Row].size()); Col++)
			{
				int32 Id = TileIndices[Row][Col];
				if (Id < 0)
					continue;

				int32 SrcX = (Id % TileColumns) * TileSize;
				int32 SrcY = (Id / TileColumns) * TileSize;
				int32 DstX = Col * TileSize;
				int32 DstY = Row * TileSize;

				G.DrawImage(Tileset, DstX, DstY, SrcX, SrcY, TileSize, TileSize, Gdiplus::UnitPixel);
			}
		}
	}
}