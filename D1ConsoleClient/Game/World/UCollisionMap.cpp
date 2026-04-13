#include "UCollisionMap.h"

#include <fstream>
#include <sstream>
#include <string>

namespace D1
{
	bool UCollisionMap::Load(const std::wstring& CsvPath)
	{
		// CSV 파싱: UTileMap::Load와 동일한 스타일 (쉼표 구분 정수, 빈 줄 무시)
		std::ifstream File(CsvPath);
		if (!File.is_open())
			return false;

		CollisionData.clear();
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
				// 규약: 0 = 통행 가능, 1 = 차단
				Row.push_back(std::stoi(Token));
			}
			CollisionData.push_back(std::move(Row));
		}

		Rows = static_cast<int32>(CollisionData.size());
		// 첫 행의 열 수를 맵 가로 폭으로 사용 (정형 격자 가정)
		Cols = Rows > 0 ? static_cast<int32>(CollisionData[0].size()) : 0;
		return Rows > 0;
	}

	bool UCollisionMap::IsBlocked(int32 TileX, int32 TileY) const
	{
		// 맵 범위 밖은 자동 차단 — 경계 이탈 방지 (deep-interview 옵션 A1)
		if (TileX < 0 || TileY < 0 || TileY >= Rows)
			return true;
		// 행마다 열 수가 다를 가능성을 대비해 해당 행의 실제 길이로 비교
		const std::vector<int32>& RowData = CollisionData[TileY];
		if (TileX >= static_cast<int32>(RowData.size()))
			return true;
		return RowData[TileX] != 0;
	}
}