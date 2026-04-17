#pragma once

#include "Core/CoreMinimal.h"

#include "Core/CoreMinimal.h"

/** GDI 더블 버퍼링 기반 렌더러 싱글톤. */
class Renderer
{
public:
	static Renderer& Get();

	/** 렌더러를 초기화한다. */
	bool Initialize(HWND InHWnd, int32 InWidth, int32 InHeight);

	/** GDI 리소스를 해제한다. */
	void Shutdown();

	/** 백버퍼를 클리어한다. 프레임 시작 시 호출. */
	void BeginRender();

	/** 백버퍼를 화면에 전송한다. 프레임 종료 시 호출. */
	void EndRender();

	HDC GetBackBufferDC() const { return BackBufferDC; }

	/** 백버퍼에 디버그용 텍스트 한 줄을 그린다. */
	void DrawDebugText(int32 X, int32 Y, const wchar_t* Text, COLORREF Color = RGB(255, 255, 255));

private:
	Renderer() = default;
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	HWND HWnd = nullptr;
	HDC ScreenDC = nullptr;
	HDC BackBufferDC = nullptr;
	HBITMAP BackBuffer = nullptr;
	HBITMAP OldBitmap = nullptr;
	int32 Width = 0;
	int32 Height = 0;
};
