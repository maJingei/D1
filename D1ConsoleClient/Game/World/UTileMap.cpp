#include "UTileMap.h"
#include "../../Subsystems/Texture.h"
#include "../../Subsystems/ResourceManager.h"

#pragma comment(lib, "msimg32.lib") // AlphaBlend

#include <fstream>
#include <sstream>
#include <string>

namespace D1
{
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
				// Tiled CSV는 1-based ID. 0은 빈 타일.
				Row.push_back(std::stoi(Token));
			}
			TileIndices.push_back(std::move(Row));
		}
		return !TileIndices.empty();
	}

	void UTileMap::Render(HDC BackDC)
	{
		if (!TilesetTexture)
			return;

		// 타일셋 DIBSection을 선택한 임시 DC 생성
		HDC TilesetDC = ::CreateCompatibleDC(BackDC);
		HBITMAP OldBitmap = static_cast<HBITMAP>(::SelectObject(TilesetDC, TilesetTexture->GetBitmap()));

		// AlphaBlend: 프리멀티플라이드 알파(AC_SRC_ALPHA)로 픽셀 단위 투명 합성
		BLENDFUNCTION Blend = {};
		Blend.BlendOp             = AC_SRC_OVER;
		Blend.BlendFlags          = 0;
		Blend.SourceConstantAlpha = 255;
		Blend.AlphaFormat         = AC_SRC_ALPHA;

		for (int32 Row = 0; Row < static_cast<int32>(TileIndices.size()); Row++)
		{
			for (int32 Col = 0; Col < static_cast<int32>(TileIndices[Row].size()); Col++)
			{
				int32 Id = TileIndices[Row][Col];
				if (Id < 0)
					continue; // -1은 빈 타일

				int32 SrcX = (Id % TileColumns) * TileSize;
				int32 SrcY = (Id / TileColumns) * TileSize;
				int32 DstX = Col * TileSize;
				int32 DstY = Row * TileSize;

				::AlphaBlend(BackDC, DstX, DstY, TileSize, TileSize,
					TilesetDC, SrcX, SrcY, TileSize, TileSize, Blend);
			}
		}

		::SelectObject(TilesetDC, OldBitmap);
		::DeleteDC(TilesetDC);
	}
}
