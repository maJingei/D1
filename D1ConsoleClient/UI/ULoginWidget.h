#pragma once

#include "Core/CoreMinimal.h"
#include "UI/UButton.h"
#include "UI/UPasswordField.h"
#include "UI/UTextField.h"
#include "UI/UWidget.h"

#include <string>

/**
 * 전체화면 로그인 위젯.
 */
class ULoginWidget : public UWidget
{
public:
	ULoginWidget();
	virtual ~ULoginWidget() = default;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(HDC BackDC, int32 AnchorX, int32 AnchorY) override;

	/** 실패 응답 수신 시: 메시지 표시 + PW 클리어 + focus PW + SetVisible(true). */
	void ShowError(const wchar_t* Message);

	/** 내부 전용 — 다음 타이핑 시 에러를 걷어낸다. */
	void ClearError();

private:
	/** ID ↔ PW focus 전환. Tab / 마우스 클릭 / ShowError 가 호출한다. */
	void SetFocus(UTextField* Target);

	/** [Login] 버튼/Enter 공용 제출 경로. 빈 값 검사 후 GameClientSession::SendLoginPacket(id, pw). */
	void SubmitLogin();

	UTextField IdField;
	UPasswordField PwField;
	UButton LoginButton;
	std::wstring ErrorMessage;

	// ---------------------------------------------------------------
	// SVG 기반 배치 상수 (×2 scale, 창 중앙 offset X=130 Y=56)
	// ---------------------------------------------------------------

	/** 스톤 패널 외곽 — SVG 90,35,160,200. */
	static constexpr int32 PanelX = 310;
	static constexpr int32 PanelY = 126;
	static constexpr int32 PanelW = 320;
	static constexpr int32 PanelH = 400;

	/** 금색 타이틀 배너 — SVG 115,48,110,20. */
	static constexpr int32 BannerX = 360;
	static constexpr int32 BannerY = 152;
	static constexpr int32 BannerW = 220;
	static constexpr int32 BannerH = 40;

	/** ID/PW 라벨 + 필드 좌표 (가로는 필드 둘 공통). */
	static constexpr int32 FieldX = 336;
	static constexpr int32 FieldW = 268;
	static constexpr int32 FieldH = 44;
	static constexpr int32 IdLabelY = 240;
	static constexpr int32 IdFieldY = 256;
	static constexpr int32 PwLabelY = 332;
	static constexpr int32 PwFieldY = 348;

	/** Login 버튼 — SVG 115,196,110,28. */
	static constexpr int32 ButtonX = 360;
	static constexpr int32 ButtonY = 448;
	static constexpr int32 ButtonW = 220;
	static constexpr int32 ButtonH = 56;

	/** 에러 라벨 영역 (버튼 아래). */
	static constexpr int32 ErrorX = 336;
	static constexpr int32 ErrorY = 518;
	static constexpr int32 ErrorW = 268;
	static constexpr int32 ErrorH = 20;
};