#include "Texture.h"

#pragma comment(lib, "gdiplus.lib")

#include <gdiplus.h>

Texture::~Texture()
{
	Release();
}

bool Texture::Load(const std::wstring& Path)
{
	// GDI+가 PNG 디코딩과 알파 보존을 모두 처리한다 — 수동 premultiply/DIBSection 변환 불필요.
	Bitmap = std::make_unique<Gdiplus::Bitmap>(Path.c_str());
	if (Bitmap->GetLastStatus() != Gdiplus::Ok)
	{
		Bitmap.reset();
		return false;
	}
	Width  = static_cast<int32>(Bitmap->GetWidth());
	Height = static_cast<int32>(Bitmap->GetHeight());
	return true;
}

void Texture::Release()
{
	Bitmap.reset();
	Width  = 0;
	Height = 0;
}
