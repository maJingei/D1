#include "UI/UPasswordField.h"

void UPasswordField::BuildDisplayText(wchar_t* OutBuffer, int32 BufferLen) const
{
	int32 i = 0;
	for (; i < TextLength && i < BufferLen - 1; ++i)
		OutBuffer[i] = L'*';
	OutBuffer[i] = L'\0';
}