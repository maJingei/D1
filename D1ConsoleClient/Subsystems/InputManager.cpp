#include "InputManager.h"

#include <cstring>

namespace D1
{
	InputManager& InputManager::Get()
	{
		static InputManager* Instance = new InputManager();
		return *Instance;
	}

	void InputManager::Initialize()
	{
		::ZeroMemory(CurrentKeyStates, sizeof(CurrentKeyStates));
		::ZeroMemory(PreviousKeyStates, sizeof(PreviousKeyStates));
		MousePosition = {};
	}

	void InputManager::Update()
	{
		::memcpy(PreviousKeyStates, CurrentKeyStates, sizeof(CurrentKeyStates));
		for (int32 i = 0; i < KEY_COUNT; ++i)
		{
			CurrentKeyStates[i] = (::GetAsyncKeyState(i) & 0x8000) != 0;
		}
		::GetCursorPos(&MousePosition);
	}

	void InputManager::Shutdown()
	{
	}
}
