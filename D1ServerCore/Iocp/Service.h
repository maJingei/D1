#pragma once

#include "IocpCore.h"
#include "../Core/ContainerTypes.h"
#include <functional>
#include <memory>

namespace D1
{
	class Session;

	/**
	 * 네트워크 서비스의 기반 클래스.
	 * IocpCore를 소유하고, SessionFactory를 통해 Session을 생성·관리한다.
	 * ServerService와 ClientService가 이를 상속한다.
	 *
	 * 반드시 std::make_shared로 생성해야 한다. (enable_shared_from_this 요구사항)
	 */
	class Service : public std::enable_shared_from_this<Service>
	{
	public:
		using SessionFactory = std::function<SessionRef()>;

		Service();
		virtual ~Service();

		/** IocpCore를 초기화한다. */
		bool Start();

		/** 모든 세션을 정리한다. */
		virtual void Stop();

		/** Session 생성 팩토리를 설정한다. */
		void SetSessionFactory(SessionFactory InFactory);

		/** 팩토리를 사용하여 Session을 생성하고 목록에 추가한다. */
		SessionRef CreateSession();

		/** Session을 목록에서 제거한다. shared_ptr 해제로 자동 소멸된다. */
		void ReleaseSession(SessionRef Target);

		/** IocpCore 포인터를 반환한다. */
		IocpCore* GetIocpCore() { return &Core; }

		/** 현재 활성 세션 수를 반환한다. */
		int32 GetSessionCount() const { return static_cast<int32>(Sessions.size()); }

		Service(const Service&) = delete;
		Service& operator=(const Service&) = delete;
		Service(Service&&) = delete;
		Service& operator=(Service&&) = delete;

	protected:
		IocpCore Core;
		Set<SessionRef> Sessions;
		SessionFactory Factory;
	};
}
