#pragma once

#include "Core/CoreMinimal.h"

#include "Core/CoreMinimal.h"

/**
 * 자주 사용하는 키보드 키를 가독성 있게 매핑한 enum.
 * VK_xxx 매크로 대신 EKey::W, EKey::Escape 형태로 사용한다.
 */
enum class EKey : uint8
{
	// 알파벳
	A = 'A', B = 'B', C = 'C', D = 'D', E = 'E',
	F = 'F', G = 'G', H = 'H', I = 'I', J = 'J',
	K = 'K', L = 'L', M = 'M', N = 'N', O = 'O',
	P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T',
	U = 'U', V = 'V', W = 'W', X = 'X', Y = 'Y',
	Z = 'Z',

	// 숫자
	Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
	Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',

	// 기능 키
	Escape    = VK_ESCAPE,
	Enter     = VK_RETURN,
	Space     = VK_SPACE,
	Tab       = VK_TAB,
	Backspace = VK_BACK,
	Shift     = VK_SHIFT,
	Control   = VK_CONTROL,
	Alt       = VK_MENU,

	// 방향키
	Left  = VK_LEFT,
	Right = VK_RIGHT,
	Up    = VK_UP,
	Down  = VK_DOWN,

	// F 키
	F1  = VK_F1,  F2  = VK_F2,  F3  = VK_F3,  F4  = VK_F4,
	F5  = VK_F5,  F6  = VK_F6,  F7  = VK_F7,  F8  = VK_F8,
	F9  = VK_F9,  F10 = VK_F10, F11 = VK_F11, F12 = VK_F12,
};

/**
 * 키보드/마우스 입력 상태를 프레임별로 관리하는 싱글톤.
 * WndProc WM_KEYDOWN/WM_KEYUP 메시지 기반으로 포커스 창에만 입력이 전달된다.
 */
class InputManager
{
public:
	static InputManager& Get();

	/** 입력 상태를 초기화한다. */
	void Initialize();

	/** 매 프레임 호출. PreviousKeyStates 를 갱신하고 마우스 좌표를 읽는다. */
	void Update();

	/** 입력 매니저를 정리한다. */
	void Shutdown();

	/** WndProc WM_KEYDOWN 에서 호출 — 해당 키를 눌린 상태로 설정한다. */
	void OnKeyDown(uint8 KeyCode);

	/** WndProc WM_KEYUP 에서 호출 — 해당 키를 뗀 상태로 설정한다. */
	void OnKeyUp(uint8 KeyCode);

	/** WndProc WM_KILLFOCUS 에서 호출 — 포커스 이탈 시 stuck-key 방지를 위해 모든 키 상태를 초기화한다. */
	void ResetAllKeys();

	/** 현재 프레임에 키가 눌려 있는지 */
	bool GetKey(uint8 KeyCode) const { return CurrentKeyStates[KeyCode]; }

	/** 이번 프레임에 키가 새로 눌렸는지 */
	bool GetKeyDown(uint8 KeyCode) const { return CurrentKeyStates[KeyCode] && !PreviousKeyStates[KeyCode]; }

	/** 이번 프레임에 키를 떼었는지 */
	bool GetKeyUp(uint8 KeyCode) const { return !CurrentKeyStates[KeyCode] && PreviousKeyStates[KeyCode]; }

	/** EKey 오버로드 — EKey::W, EKey::Escape 등으로 직접 사용 가능 */
	bool GetKey(EKey Key) const { return GetKey(static_cast<uint8>(Key)); }
	bool GetKeyDown(EKey Key) const { return GetKeyDown(static_cast<uint8>(Key)); }
	bool GetKeyUp(EKey Key) const { return GetKeyUp(static_cast<uint8>(Key)); }

	POINT GetMousePosition() const { return MousePosition; }

private:
	InputManager() = default;
	~InputManager();
	InputManager(const InputManager&) = delete;
	InputManager& operator=(const InputManager&) = delete;

	static constexpr int32 KEY_COUNT = 256;
	bool CurrentKeyStates[KEY_COUNT] = {};
	bool PreviousKeyStates[KEY_COUNT] = {};
	POINT MousePosition = {};
};
