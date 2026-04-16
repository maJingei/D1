#include "UCollisionMap.h"

#include <fstream>
#include <sstream>
#include <string>

namespace
{
	/** 열린 ifstream에서 CSV 행을 파싱해 CollisionData/Rows/Cols 로 채운다. */
	bool ParseCsv(std::ifstream& File, std::vector<std::vector<int32>>& OutData, int32& OutRows, int32& OutCols)
	{
		if (!File.is_open())
			return false;

		OutData.clear();
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
			OutData.push_back(std::move(Row));
		}

		OutRows = static_cast<int32>(OutData.size());
		// 첫 행의 열 수를 맵 가로 폭으로 사용 (정형 격자 가정)
		OutCols = OutRows > 0 ? static_cast<int32>(OutData[0].size()) : 0;
		return OutRows > 0;
	}
}

bool UCollisionMap::Load(const std::wstring& CsvPath)
{
	// MSVC는 std::ifstream(wchar_t*) 오버로드를 지원하므로 wide 경로도 직접 사용 가능.
	std::ifstream File(CsvPath);
	return ParseCsv(File, CollisionData, Rows, Cols);
}

bool UCollisionMap::Load(const std::string& CsvPath)
{
	std::ifstream File(CsvPath);
	return ParseCsv(File, CollisionData, Rows, Cols);
}

bool UCollisionMap::IsBlocked(int32 TileX, int32 TileY) const
{
	// 맵 범위 밖은 자동 차단 — 경계 이탈 방지
	if (TileX < 0 || TileY < 0 || TileY >= Rows)
		return true;
	// 행마다 열 수가 다를 가능성을 대비해 해당 행의 실제 길이로 비교
	const std::vector<int32>& RowData = CollisionData[TileY];
	if (TileX >= static_cast<int32>(RowData.size()))
		return true;
	return RowData[TileX] != 0;
}
