#include "Renderer.h"

namespace D1
{
	Renderer& Renderer::Get()
	{
		// Meyers singleton: 프로세스 종료 시 dtor가 자동 호출되어 GDI 리소스를 해제한다.
		static Renderer Instance;
		return Instance;
	}

	Renderer::~Renderer()
	{
		// 명시적 Shutdown 누락 시 안전망. Shutdown은 멱등이므로 중복 호출 무해.
		Shutdown();
	}

	bool Renderer::Initialize(HWND InHWnd, int32 InWidth, int32 InHeight)
	{
		HWnd = InHWnd;
		Width = InWidth;
		Height = InHeight;
		// GDI 더블 버퍼 생성. 부분 실패 시 이미 할당된 리소스를 역순 해제.
		ScreenDC = ::GetDC(HWnd);
		if (!ScreenDC)
		{
			return false;
		}
		BackBufferDC = ::CreateCompatibleDC(ScreenDC);
		if (!BackBufferDC)
		{
			::ReleaseDC(HWnd, ScreenDC);
			ScreenDC = nullptr;
			return false;
		}
		BackBuffer = ::CreateCompatibleBitmap(ScreenDC, Width, Height);
		if (!BackBuffer)
		{
			::DeleteDC(BackBufferDC);
			BackBufferDC = nullptr;
			::ReleaseDC(HWnd, ScreenDC);
			ScreenDC = nullptr;
			return false;
		}
		OldBitmap = static_cast<HBITMAP>(::SelectObject(BackBufferDC, BackBuffer));
		return true;
	}

	void Renderer::Shutdown()
	{
		if (BackBufferDC && OldBitmap)
		{
			::SelectObject(BackBufferDC, OldBitmap);
			OldBitmap = nullptr;
		}
		if (BackBuffer)
		{
			::DeleteObject(BackBuffer);
			BackBuffer = nullptr;
		}
		if (BackBufferDC)
		{
			::DeleteDC(BackBufferDC);
			BackBufferDC = nullptr;
		}
		if (ScreenDC && HWnd)
		{
			::ReleaseDC(HWnd, ScreenDC);
			ScreenDC = nullptr;
		}
	}

	void Renderer::BeginRender()
	{
		RECT Rect = { 0, 0, Width, Height };
		::FillRect(BackBufferDC, &Rect, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
	}

	void Renderer::EndRender()
	{
		::BitBlt(ScreenDC, 0, 0, Width, Height, BackBufferDC, 0, 0, SRCCOPY);
	}
}
