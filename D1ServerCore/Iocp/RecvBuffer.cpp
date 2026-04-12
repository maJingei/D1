#include "RecvBuffer.h"
#include <cstring>

namespace D1
{
	RecvBuffer::RecvBuffer(int32 InCapacity) : Capacity(InCapacity), Buffer(static_cast<size_t>(InCapacity))
	{
	}

	void RecvBuffer::Clean()
	{
		const int32 DataSize = GetDataSize();
		if (DataSize == 0)
		{
			// 빈 버퍼: 양쪽 커서를 0으로 되돌려 전체 공간 재사용
			ReadPos  = 0;
			WritePos = 0;
			return;
		}

		// 잔여 데이터를 버퍼 앞으로 끌어당겨 뒤쪽 공간을 확보한다
		std::memmove(&Buffer[0], &Buffer[ReadPos], static_cast<size_t>(DataSize));
		ReadPos  = 0;
		WritePos = DataSize;
	}

	bool RecvBuffer::OnRead(int32 NumOfBytes)
	{
		if (NumOfBytes < 0 || NumOfBytes > GetDataSize())
		{
			return false;
		}
		ReadPos += NumOfBytes;
		return true;
	}

	bool RecvBuffer::OnWrite(int32 NumOfBytes)
	{
		if (NumOfBytes < 0 || NumOfBytes > GetFreeSize())
		{
			return false;
		}
		WritePos += NumOfBytes;
		return true;
	}
}