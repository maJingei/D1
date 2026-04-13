#pragma once

#include <Windows.h>
#include "Core/Types.h"

namespace D1
{
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

	protected:
		float X = 0.f;
		float Y = 0.f;
	};
}