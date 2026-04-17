#pragma once

#include "Core/CoreMinimal.h"
#include <memory>
#include <map>

#include "Core/CoreMinimal.h"

class Texture;

/** 스프라이트 시트 기반 프레임 애니메이션을 관리하는 클래스. */
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

	/** 정사각형 프레임용 초기화 (기존 호출 호환). */
	void Init(std::shared_ptr<Texture> InTexture, int32 InFrameSize);

	/** 직사각형 프레임용 초기화. */
	void Init(std::shared_ptr<Texture> InTexture, int32 InFrameW, int32 InFrameH);

	/** 정사각형 출력 크기 설정. RenderW = RenderH = InRenderSize. */
	void SetRenderSize(int32 InRenderSize) { RenderW = InRenderSize; RenderH = InRenderSize; }

	/** 직사각형 출력 크기 설정. */
	void SetRenderSize(int32 InRenderW, int32 InRenderH) { RenderW = InRenderW; RenderH = InRenderH; }

	/** 지정한 색을 완전 투명으로 처리한다. */
	void SetColorKey(uint8 R, uint8 G, uint8 B);

	/** 색 키가 활성화돼 있는지 여부. */
	bool HasColorKey() const { return bUseColorKey; }

	/** 애니메이션 클립을 ID로 등록한다. */
	void AddClip(int32 ClipId, FAnimClip Clip);

	/** 재생할 클립을 전환한다. */
	void SetClipId(int32 ClipId);

	/** 경과 시간만큼 현재 클립의 프레임을 진행한다. */
	void Update(float DeltaTime);

	/** 현재 재생 중인 클립 ID. 재생 중 클립이 없으면 -1. */
	int32 GetCurrentClip() const { return CurrentClip; }

	/** 현재 프레임 인덱스(0-based). 재생 중 클립이 없으면 0. */
	int32 GetCurrentFrame() const { return CurrentFrame; }

	/** 현재 재생 사이클이 마지막 프레임에서 막 끝나 다음 Update에 루프로 돌아가는지 여부. */
	bool IsOnLastFrame() const;

	/** 현재 재생 중인 클립의 총 프레임 수. 클립이 없으면 0. */
	int32 GetCurrentClipFrameCount() const;

	/** 현재 프레임을 BackDC에 GDI+ DrawImage로 렌더링한다. */
	void Render(HDC BackDC, int32 X, int32 Y, bool bFlipH = false);

private:
	std::shared_ptr<Texture>    SpriteTexture;
	std::map<int32, FAnimClip>  Clips;

	int32 FrameW  = 32;
	int32 FrameH  = 32;

	/** 화면 출력 픽셀 크기. Frame 크기와 다르면 NearestNeighbor 확대로 픽셀 아트 보존. */
	int32 RenderW = 32;
	int32 RenderH = 32;

	int32 CurrentClip  = -1;
	int32 CurrentFrame = 0;
	float AnimTimer    = 0.f;

	/** 컬러 키 활성 여부. true면 Render에서 ImageAttributes로 투명 처리. */
	bool  bUseColorKey = false;
	uint8 ColorKeyR = 0;
	uint8 ColorKeyG = 0;
	uint8 ColorKeyB = 0;
};