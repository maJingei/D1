#pragma once

#include <Windows.h>

#include "Core/Types.h"

namespace D1
{
	/**
	 * 키보드/마우스 입력 상태를 프레임별로 관리하는 싱글톤.
	 * GetAsyncKeyState 기반으로 매 프레임 키 상태를 스캔한다.
	 */
	class InputManager
	{
	public:
		static InputManager& Get();

		/** 입력 상태를 초기화한다. */
		void Initialize();

		/** 매 프레임 호출. 키보드/마우스 상태를 갱신한다. */
		void Update();

		/** 입력 매니저를 정리한다. */
		void Shutdown();

		/** 현재 프레임에 키가 눌려 있는지 */
		bool GetKey(uint8 KeyCode) const { return CurrentKeyStates[KeyCode]; }

		/** 이번 프레임에 키가 새로 눌렸는지 */
		bool GetKeyDown(uint8 KeyCode) const { return CurrentKeyStates[KeyCode] && !PreviousKeyStates[KeyCode]; }

		/** 이번 프레임에 키를 떼었는지 */
		bool GetKeyUp(uint8 KeyCode) const { return !CurrentKeyStates[KeyCode] && PreviousKeyStates[KeyCode]; }

		POINT GetMousePosition() const { return MousePosition; }

	private:
		InputManager() = default;
		~InputManager() = default;
		InputManager(const InputManager&) = delete;
		InputManager& operator=(const InputManager&) = delete;

		static constexpr int32 KEY_COUNT = 256;
		bool CurrentKeyStates[KEY_COUNT] = {};
		bool PreviousKeyStates[KEY_COUNT] = {};
		POINT MousePosition = {};
	};
}
