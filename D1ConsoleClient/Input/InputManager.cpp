#include "InputManager.h"

#include <cstring>

InputManager& InputManager::Get()
{
	// Meyers singleton: 프로세스 종료 시 dtor 자동 호출.
	static InputManager Instance;
	return Instance;
}

InputManager::~InputManager()
{
	Shutdown();
}

void InputManager::Initialize()
{
	::ZeroMemory(CurrentKeyStates, sizeof(CurrentKeyStates));
	::ZeroMemory(PreviousKeyStates, sizeof(PreviousKeyStates));
	MousePosition = {};
}

void InputManager::Update()
{
	// 이전 프레임 상태 보존 (GetKeyDown/GetKeyUp 계산에 사용).
	// 키 상태 갱신은 WndProc OnKeyDown/OnKeyUp 이 담당하므로 여기서는 복사만 수행한다.
	::memcpy(PreviousKeyStates, CurrentKeyStates, sizeof(CurrentKeyStates));
	::GetCursorPos(&MousePosition);
}

void InputManager::OnKeyDown(uint8 KeyCode)
{
	if (KeyCode < KEY_COUNT)
		CurrentKeyStates[KeyCode] = true;
}

void InputManager::OnKeyUp(uint8 KeyCode)
{
	if (KeyCode < KEY_COUNT)
		CurrentKeyStates[KeyCode] = false;
}

void InputManager::ResetAllKeys()
{
	::ZeroMemory(CurrentKeyStates, sizeof(CurrentKeyStates));
}

void InputManager::Shutdown()
{
}
