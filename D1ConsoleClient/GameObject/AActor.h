#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CoreMinimal.h"

class UWorld;

/**
 * 타일맵 탑다운 기준의 4방향 Facing.
 * 이동/공격 등 방향성을 가진 로직에서 공용으로 사용한다.
 */
enum class EDirection : uint8
{
	Up = 0,
	Down,
	Left,
	Right,
};

/**
 * 모든 게임 오브젝트의 최상위 베이스 클래스.
 * UWorld가 관리하는 단위이며, Tick/Render 인터페이스를 제공한다.
 */
class AActor
{
public:
	virtual ~AActor() = default;

	/** 매 프레임 호출. 오브젝트 로직을 처리한다. */
	virtual void Tick(float DeltaTime) {}

	/** 매 프레임 호출. 백버퍼 DC에 오브젝트를 그린다. */
	virtual void Render(HDC BackDC) {}

	float GetX() const { return X; }
	float GetY() const { return Y; }
	void SetPosition(float InX, float InY) { X = InX; Y = InY; }

	/** UWorld::SpawnActor가 액터 등록 시 소속 월드를 주입한다. */
	void SetWorld(UWorld* InWorld) { World = InWorld; }

	/** 소속 월드를 반환한다 (월드 외부 생성된 경우 nullptr). */
	UWorld* GetWorld() const { return World; }

	/**
	 * 현재 논리 타일 X 좌표. 서브클래스는 자신의 TilePos를 사용하도록 override한다.
	 * 기본 구현은 픽셀 좌표를 BaseTileSize로 나눈 값을 반환한다.
	 */
	virtual int32 GetTileX() const { return static_cast<int32>(X) / BaseTileSize; }

	/**
	 * 현재 논리 타일 Y 좌표. 서브클래스는 자신의 TilePos를 사용하도록 override한다.
	 * 기본 구현은 픽셀 좌표를 BaseTileSize로 나눈 값을 반환한다.
	 */
	virtual int32 GetTileY() const { return static_cast<int32>(Y) / BaseTileSize; }

	/** true이면 다른 액터가 이 액터가 점유한 타일로 이동할 수 없다. */
	bool bBlocksMovement = true;

protected:
	float X = 0.f;
	float Y = 0.f;

	/** 소속 월드. raw 포인터 — 액터의 수명은 UWorld가 소유하므로 World가 더 오래 산다. */
	UWorld* World = nullptr;

	/** 타일 픽셀 크기 기본값. GetTileX/Y 기본 구현에서 사용한다. */
	static constexpr int32 BaseTileSize = 32;
};
