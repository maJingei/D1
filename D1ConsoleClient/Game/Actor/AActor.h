#pragma once

#include <Windows.h>
#include "Core/Types.h"

namespace D1
{
	class UWorld;

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

	protected:
		float X = 0.f;
		float Y = 0.f;

		/** 소속 월드. raw 포인터 — 액터의 수명은 UWorld가 소유하므로 World가 더 오래 산다. */
		UWorld* World = nullptr;
	};
}