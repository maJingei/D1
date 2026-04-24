#pragma once

#include "Core/CoreMinimal.h"

#include <functional>
#include <memory>
#include <utility>

/** 실행 가능한 작업 단위. */
class Job
{
public:
	/** 콜백 버전 생성자: 이미 준비된 callable 을 그대로 저장한다. */
	Job(std::function<void()> Callback)
		: Callback(std::move(Callback))
	{
	}

	/**
	 * 템플릿 버전 생성자: 멤버 함수 포인터 + shared_ptr + 인자 목록을 받아 내부에서 람다를 만든다.
	 */
	template<typename T, typename Ret, typename... Args, typename... ActualArgs>
	Job(std::shared_ptr<T> Owner, Ret(T::*MemFunc)(Args...), ActualArgs&&... InArgs)
	{
		// Owner 는 weak_ptr 로 잡아 Execute 시점에 소멸했으면 호출을 건너뛴다.
		std::weak_ptr<T> WeakOwner = Owner;
		
		Callback = [WeakOwner, MemFunc, ...InArgs = std::forward<ActualArgs>(InArgs)]() mutable
		{
			std::shared_ptr<T> Locked = WeakOwner.lock();
			if (Locked == nullptr)
				return;
			// 캡처된 인자를 std::move 로 풀어 MemFunc 에 그대로 전달한다.
			((*Locked).*MemFunc)(std::move(InArgs)...);
		};
	}

	/** 저장된 콜백을 실행한다. */
	void Execute() { Callback(); }

private:
	std::function<void()> Callback;
};
