#pragma once

#include "../Core/Types.h"

#include <functional>
#include <memory>

namespace D1
{
	/**
	 * 실행 가능한 작업 단위. std::function<void()> 콜백을 저장하고 Execute() 로 호출한다.
	 *
	 * 두 가지 생성 방법을 제공한다:
	 *   1. 콜백 버전: 이미 캡처가 완료된 std::function<void()> 을 직접 전달.
	 *   2. 템플릿 버전: 멤버 함수 포인터 + shared_ptr Owner + 인자들을 받아
	 *      내부에서 람다로 Callback 을 구성한다. Owner 를 weak_ptr 로 캡처하여
	 *      Job 실행 시점에 Owner 가 이미 소멸했을 경우 안전하게 skip 한다.
	 */
	class Job
	{
	public:
		/** 콜백 버전 생성자: 이미 준비된 callable 을 그대로 저장한다. */
		explicit Job(std::function<void()> Callback)
			: Callback(std::move(Callback))
		{
		}

		/**
		 * 템플릿 버전 생성자: 멤버 함수 포인터 + shared_ptr + 인자 목록을 받아 내부에서 람다를 만든다.
		 *
		 * Owner 를 weak_ptr 로 캡처 — Execute 시점에 Owner 가 소멸했으면 호출을 skip 한다.
		 * 인자들은 값으로 캡처하므로 호출자 스택 변수를 안전하게 복사한다.
		 *
		 * @param Owner    멤버 함수를 소유하는 객체 (shared_ptr)
		 * @param MemFunc  호출할 멤버 함수 포인터
		 * @param args     멤버 함수에 전달할 인자들 (값 복사)
		 */
		template<typename T, typename Ret, typename... Args>
		Job(std::shared_ptr<T> Owner, Ret(T::*MemFunc)(Args...), Args... args)
		{
			// Owner 를 weak_ptr 로, 인자를 값으로 캡처한다.
			// Execute 시점에 Owner 가 소멸했으면 호출을 건너뛴다.
			std::weak_ptr<T> WeakOwner = Owner;
			Callback = [WeakOwner, MemFunc, args...]()
			{
				std::shared_ptr<T> Locked = WeakOwner.lock();
				if (Locked == nullptr)
					return;
				((*Locked).*MemFunc)(args...);
			};
		}

		/** 저장된 콜백을 실행한다. */
		void Execute() { Callback(); }

	private:
		std::function<void()> Callback;
	};
}