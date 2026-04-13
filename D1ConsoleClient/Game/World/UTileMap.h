#pragma once

#include <Windows.h>
#include <vector>
#include <memory>
#include <string>

#include "Core/Types.h"

namespace D1
{
	class Texture;

	/**
	 * CSV 타일맵 데이터와 타일셋 Texture를 보유하고 배경으로 렌더링한다.
	 * UWorld가 소유하며, Render 시 Actor보다 먼저 그려진다.
	 */
	class UTileMap
	{
	public:
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
		std::shared_ptr<Texture> TilesetTexture;
		std::vector<std::vector<int32>> TileIndices; // [행][열], Tiled 1-based ID (0 = 빈 칸)
		int32 TileColumns = 16;
		int32 TileSize = 32;
	};
}