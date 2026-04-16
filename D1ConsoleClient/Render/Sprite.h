#pragma once

#include "Core/CoreMinimal.h"
#include <memory>
#include <map>

#include "Core/CoreMinimal.h"

class Texture;

/**
 * 스프라이트 시트 기반 프레임 애니메이션을 관리하는 클래스.
 * 여러 애니메이션 클립(행, 프레임 수, FPS)을 ID로 등록하고,
 * Play/Update/Render 인터페이스로 Actor에서 사용한다.
 */
class Sprite
{
public:
	/** 애니메이션 클립 정의. 스프라이트 시트의 한 행에 해당한다. */
	struct FAnimClip
	{
		int32 Row;        /** 스프라이트 시트 행(0-indexed) */
		int32 FrameCount; /** 클립의 총 프레임 수 */
		float Fps;        /** 초당 프레임 수 */
	};

public:
	Sprite() = default;
	~Sprite() = default;

	/**
	 * 스프라이트 시트 텍스처와 프레임 크기를 설정한다. RenderSize는 InFrameSize와 동일 값으로 초기화된다 (1:1 출력).
	 *
	 * @param InTexture   ResourceManager에서 로드된 스프라이트 시트 Texture
	 * @param InFrameSize 시트에서 한 프레임을 잘라낼 픽셀 크기 (정사각형 가정)
	 */
	void Init(std::shared_ptr<Texture> InTexture, int32 InFrameSize);

	/**
	 * 화면 출력 크기를 변경한다 (FrameSize와 다르면 확대/축소 출력). 픽셀 아트는 NearestNeighbor 보간으로 선명하게 확대된다.
	 */
	void SetRenderSize(int32 InRenderSize) { RenderSize = InRenderSize; }

	/**
	 * 애니메이션 클립을 ID로 등록한다.
	 *
	 * @param ClipId 호출자 정의 식별자 (enum을 int32로 캐스팅)
	 * @param Clip   클립 정의 (행, 프레임 수, FPS)
	 */
	void AddClip(int32 ClipId, FAnimClip Clip);

	/**
	 * 재생할 클립을 전환한다. 이미 같은 클립이 재생 중이면 무시하여 프레임이 리셋되지 않는다.
	 */
	void SetClipId(int32 ClipId);

	/** 경과 시간만큼 현재 클립의 프레임을 진행한다. */
	void Update(float DeltaTime);

	/** 현재 재생 중인 클립 ID. 재생 중 클립이 없으면 -1. */
	int32 GetCurrentClip() const { return CurrentClip; }

	/** 현재 프레임 인덱스(0-based). 재생 중 클립이 없으면 0. */
	int32 GetCurrentFrame() const { return CurrentFrame; }

	/** 현재 재생 사이클이 마지막 프레임에서 막 끝나 다음 Update에 루프로 돌아가는지 여부. */
	bool IsOnLastFrame() const;

	/**
	 * 현재 프레임을 BackDC에 GDI+ DrawImage로 렌더링한다.
	 *
	 * @param bFlipH true 이면 수평 반전(좌향) 렌더링
	 */
	void Render(HDC BackDC, int32 X, int32 Y, bool bFlipH = false);

private:
	std::shared_ptr<Texture>    SpriteTexture;
	std::map<int32, FAnimClip>  Clips;

	int32 FrameSize    = 32;
	
	/** 화면 출력 픽셀 크기. FrameSize와 다르면 NearestNeighbor 확대로 픽셀 아트 보존. */
	int32 RenderSize   = 32;
	int32 CurrentClip  = -1;
	int32 CurrentFrame = 0;
	float AnimTimer    = 0.f;
};
