#include "IocpObject.h"
#include "IocpEvent.h"
#include <cassert>

namespace D1
{
	void IocpObject::HoldForIo(IocpEvent& Event)
	{
#ifdef _DEBUG
		// Dispatch 진입 시 std::move로 Owner가 비워진 불변식에 의존한다.
		// 이 assert가 걸리면 해당 Event가 이전 I/O 완료 없이 재게시됐음을 의미.
		assert(!Event.Owner);
#endif
		Event.Owner = shared_from_this();
		Event.Init();
	}
}