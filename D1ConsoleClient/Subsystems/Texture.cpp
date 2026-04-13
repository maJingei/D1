#include "Texture.h"

#pragma comment(lib, "gdiplus.lib")

#include <gdiplus.h>

namespace
{
	/** 32비트 프리멀티플라이드 알파 DIBSection을 생성한다. */
	bool CreateAlphaDIB(int32 Width, int32 Height, HBITMAP& OutBitmap, void** OutBits)
	{
		BITMAPINFO bmi              = {};
		bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth       = Width;
		bmi.bmiHeader.biHeight      = -Height; // top-down
		bmi.bmiHeader.biPlanes      = 1;
		bmi.bmiHeader.biBitCount    = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		OutBitmap = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, OutBits, nullptr, 0);
		return OutBitmap != nullptr && *OutBits != nullptr;
	}

	/**
	 * GDI+ 비트맵 픽셀을 읽어 프리멀티플라이드 알파 형식으로 DIB 버퍼에 복사한다.
	 * AlphaBlend(AC_SRC_ALPHA) 는 R*A/255, G*A/255, B*A/255, A 형식을 요구한다.
	 */
	bool CopyPremultipliedPixels(Gdiplus::Bitmap& GdiBitmap, void* pDst, int32 Width, int32 Height)
	{
		Gdiplus::BitmapData bmpData;
		Gdiplus::Rect       rect(0, 0, Width, Height);
		if (GdiBitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bmpData) != Gdiplus::Ok)
			return false;

		const int32 DstStride = Width * 4; // 32bpp DIB: Width*4 바이트
		BYTE*       src       = static_cast<BYTE*>(bmpData.Scan0);
		BYTE*       dst       = static_cast<BYTE*>(pDst);

		for (int32 y = 0; y < Height; y++)
		{
			for (int32 x = 0; x < Width; x++)
			{
				// GDI+ PixelFormat32bppARGB 메모리 순서: B, G, R, A
				BYTE* s = src + y * bmpData.Stride + x * 4;
				BYTE* d = dst + y * DstStride       + x * 4;
				BYTE  a = s[3];
				d[0] = static_cast<BYTE>(s[0] * a / 255); // B
				d[1] = static_cast<BYTE>(s[1] * a / 255); // G
				d[2] = static_cast<BYTE>(s[2] * a / 255); // R
				d[3] = a;
			}
		}

		GdiBitmap.UnlockBits(&bmpData);
		return true;
	}
}

namespace D1
{
	Texture::~Texture()
	{
		Release();
	}

	bool Texture::Load(const std::wstring& Path)
	{
		Gdiplus::Bitmap GdiBitmap(Path.c_str());
		if (GdiBitmap.GetLastStatus() != Gdiplus::Ok)
			return false;

		Width  = static_cast<int32>(GdiBitmap.GetWidth());
		Height = static_cast<int32>(GdiBitmap.GetHeight());

		void* pBits = nullptr;
		if (!CreateAlphaDIB(Width, Height, Bitmap, &pBits))
			return false;

		return CopyPremultipliedPixels(GdiBitmap, pBits, Width, Height);
	}

	void Texture::Release()
	{
		if (Bitmap)
		{
			::DeleteObject(Bitmap);
			Bitmap = nullptr;
		}
		Width  = 0;
		Height = 0;
	}
}