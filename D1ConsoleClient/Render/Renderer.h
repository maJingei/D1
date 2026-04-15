#pragma once

#include <Windows.h>

#include "Core/Types.h"

namespace D1
{
	/**
	 * GDI 더블 버퍼링 기반 렌더러 싱글톤.
	 * 백버퍼에 그린 후 BitBlt로 화면에 전송한다.
	 */
	class Renderer
	{
	public:
		static Renderer& Get();

		/**
		 * 렌더러를 초기화한다. GDI 더블 버퍼를 생성한다.
		 *
		 * @param InHWnd    렌더링 대상 윈도우 핸들
		 * @param InWidth   백버퍼 너비
		 * @param InHeight  백버퍼 높이
		 * @return          초기화 성공 여부
		 */
		bool Initialize(HWND InHWnd, int32 InWidth, int32 InHeight);

		/** GDI 리소스를 해제한다. */
		void Shutdown();

		/** 백버퍼를 클리어한다. 프레임 시작 시 호출. */
		void BeginRender();

		/** 백버퍼를 화면에 전송한다. 프레임 종료 시 호출. */
		void EndRender();

		HDC GetBackBufferDC() const { return BackBufferDC; }

		/**
		 * 백버퍼에 디버그용 텍스트 한 줄을 그린다. 배경 투명, 기본 시스템 폰트 사용.
		 *
		 * @param X      픽셀 좌상단 X
		 * @param Y      픽셀 좌상단 Y
		 * @param Text   출력할 wide 문자열 (null-terminated)
		 * @param Color  글자 색 (기본: 흰색)
		 */
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
}
