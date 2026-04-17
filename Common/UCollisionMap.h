#pragma once

#include <vector>
#include <string>

#include "Core/Types.h"

/** 타일 단위 충돌 정보를 보유하는 경량 맵. */
class UCollisionMap
{
public:
	UCollisionMap() = default;
	~UCollisionMap() = default;

	/** 충돌 CSV를 로드한다. */
	bool Load(const std::wstring& CsvPath);

	/** 충돌 CSV를 로드한다 (narrow 경로 — 서버 경로용). */
	bool Load(const std::string& CsvPath);

	/** 지정한 타일 좌표가 차단되었는지 질의한다. */
	bool IsBlocked(int32 TileX, int32 TileY) const;

	int32 GetCols() const { return Cols; }
	int32 GetRows() const { return Rows; }

private:
	/** [행][열] 충돌값. 0=통행, 1=차단. */
	std::vector<std::vector<int32>> CollisionData;
	int32 Rows = 0;
	int32 Cols = 0;
};
