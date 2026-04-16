#pragma once

#include "Core/CoreMinimal.h"

/**
 * 서버 측 게임 오브젝트의 최상위 베이스 클래스.
 * 클라이언트 AActor와 대칭 구조로 설계되며, 렌더링 없이 논리 상태만 보유한다.
 * Health/ApplyDamage는 이 베이스에 정의하여 모든 액터가 공통으로 사용할 수 있게 한다.
 */
class AActor
{
public:
	AActor(int32 InTileX, int32 InTileY)
		: TileX(InTileX), TileY(InTileY)
	{}
	virtual ~AActor() = default;

	/** 현재 논리 타일 X 좌표 */
	virtual int32 GetTileX() const { return TileX; }

	/** 현재 논리 타일 Y 좌표 */
	virtual int32 GetTileY() const { return TileY; }

	void SetTile(int32 InTileX, int32 InTileY) { TileX = InTileX; TileY = InTileY; }

	/** 현재 체력을 반환한다. */
	int32 GetHP() const { return HP; }

	/** 최대 체력을 반환한다. */
	int32 GetMaxHP() const { return MaxHP; }

	/** 체력을 초기화한다. 파생 클래스 생성자에서 호출한다. */
	void InitHP(int32 InMaxHP) { MaxHP = InMaxHP; HP = InMaxHP; }

	/**
	 * 데미지를 적용한다. 공격자가 데미지 양을 결정하여 전달한다.
	 *
	 * @param Damage  적용할 데미지 양 (양수)
	 * @return        적용 후 남은 HP
	 */
	int32 ApplyDamage(int32 Damage)
	{
		HP -= Damage;
		if (HP < 0) HP = 0;
		return HP;
	}

	/** HP가 0 이하이면 사망 상태로 간주한다. */
	bool IsDead() const { return HP <= 0; }

protected:
	int32 TileX = 0;
	int32 TileY = 0;

	int32 HP = 0;
	int32 MaxHP = 0;
};
