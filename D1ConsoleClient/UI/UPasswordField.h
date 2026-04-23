#pragma once

#include "UI/UTextField.h"

/** 비밀번호 필드 — 내부 Text 는 평문 유지, 화면 표시만 '*' 로 치환. */
class UPasswordField : public UTextField
{
public:
	UPasswordField() = default;
	virtual ~UPasswordField() = default;

protected:
	virtual void BuildDisplayText(wchar_t* OutBuffer, int32 BufferLen) const override;
};