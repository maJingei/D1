#pragma once

#include <vector>
#include <string>

#include "Core/Types.h"

namespace D1
{
	/**
	 * 타일 단위 충돌 정보를 보유하는 경량 맵.
	 * CSV에서 0=통행 가능, 1=차단으로 로드하며 렌더링 책임은 없다.
	 * 서버(GameRoom)와 클라이언트(APlayerActor/UWorld) 양쪽에서 동일 CSV를 공유해 사용한다.
	 */
	class UCollisionMap
	{
	public:
		UCollisionMap() = default;
		~UCollisionMap() = default;

		/**
		 * 충돌 CSV를 로드한다. 0=통행, 1=차단. (wide 경로 — 클라이언트 Resource 경로용)
		 *
		 * @param CsvPath  충돌 CSV 파일 경로
		 * @return         로드 성공 여부 (파일 열기 성공 + 최소 1행 파싱)
		 */
		bool Load(const std::wstring& CsvPath);

		/**
		 * 충돌 CSV를 로드한다 (narrow 경로 — 서버 경로용).
		 *
		 * @param CsvPath  충돌 CSV 파일 경로
		 * @return         로드 성공 여부 (파일 열기 성공 + 최소 1행 파싱)
		 */
		bool Load(const std::string& CsvPath);

		/**
		 * 지정한 타일 좌표가 차단되었는지 질의한다.
		 * 맵 범위 밖 좌표는 안전하게 true(차단)로 간주한다.
		 *
		 * @param TileX  타일 열 인덱스
		 * @param TileY  타일 행 인덱스
		 * @return       차단이면 true, 통행 가능하면 false
		 */
		bool IsBlocked(int32 TileX, int32 TileY) const;

		int32 GetCols() const { return Cols; }
		int32 GetRows() const { return Rows; }

	private:
		/** [행][열] 충돌값. 0=통행, 1=차단. */
		std::vector<std::vector<int32>> CollisionData;
		int32 Rows = 0;
		int32 Cols = 0;
	};
}