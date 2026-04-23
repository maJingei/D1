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

	// 문자/클릭 버퍼는 한 프레임 수명 — PeekMessage 루프가 다음으로 채울 공간을 비운다.
	CharBuffer.clear();
	bHasPendingClick = false;
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

void InputManager::OnChar(wchar_t Char)
{
	// 인쇄 가능한 ASCII (0x20 ~ 0x7E) 만 허용. 제어문자·IME 확장·한글 모두 여기서 차단한다.
	// Backspace(0x08)·Tab(0x09)·Enter(0x0D) 는 WM_KEYDOWN 경로로 별도 처리하므로 여기 통과시키지 않는다.
	if (Char < 0x20 || Char > 0x7E)
		return;
	CharBuffer.push_back(Char);
}

void InputManager::OnMouseDown(int32 X, int32 Y)
{
	PendingClickX = X;
	PendingClickY = Y;
	bHasPendingClick = true;
}

bool InputManager::ConsumeMouseDown(int32& OutX, int32& OutY)
{
	if (bHasPendingClick == false)
		return false;
	OutX = PendingClickX;
	OutY = PendingClickY;
	bHasPendingClick = false;
	return true;
}

void InputManager::Shutdown()
{
}
