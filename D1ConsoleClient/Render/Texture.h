#pragma once

#include <memory>
#include <string>

#include "Core/CoreMinimal.h"

namespace Gdiplus { class Bitmap; }

/**
 * GDI+ Bitmap을 보유하는 리소스 단위.
 * ResourceManager가 생성·캐싱하며, 여러 오브젝트가 shared_ptr로 공유한다.
 */
class Texture
{
public:
	~Texture();

	/** 지정 경로의 이미지를 GDI+로 로드한다. 성공 시 true 반환. */
	bool Load(const std::wstring& Path);

	/** 보유한 GDI+ 리소스를 해제한다. */
	void Release();

	Gdiplus::Bitmap* GetBitmap() const { return Bitmap.get(); }
	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }

private:
	std::unique_ptr<Gdiplus::Bitmap> Bitmap;
	int32 Width = 0;
	int32 Height = 0;
};
