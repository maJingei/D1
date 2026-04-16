#pragma once

#include "Core/CoreMinimal.h"
#include "Iocp/Service.h"
#include "Iocp/Listener.h"
#include "Iocp/NetAddress.h"
#include <utility>

/**
 * 서버 측 Service.
 * Listener를 소유하여 클라이언트 접속을 수용하고,
 * SessionFactory로 생성한 Session을 관리한다.
 *
 * Listener는 IocpObject 기반으로 shared_ptr 관리가 필수다
 * (enable_shared_from_this<IocpObject> infectious tax).
 */
class ServerService : public Service
{
public:
	/**
	 * @param InAddress  바인드할 주소 (IP + Port)
	 * @param InFactory  Session 생성 팩토리
	 */
	ServerService(NetAddress InAddress, SessionFactory InFactory)
		: Service(InAddress, std::move(InFactory)) {}

	/**
	 * IocpCore 초기화 + Listener 시작. (멤버 Address 사용)
	 *
	 * @return 시작 성공 여부
	 */
	bool Start() override;

	/**
	 * Listener 중지 + 모든 세션 정리 + drain 완료까지 block.
	 *
	 * 불변식(P6): 이 함수는 IOCP 워커가 살아있는 구간에서 호출돼야 한다.
	 * closesocket이 유발하는 error completion은 Dispatch 워커가 dequeue해야
	 * KeepAlive 경유로 IocpObject가 소멸 가능하기 때문이다.
	 */
	void Stop() override;

private:
	ListenerRef ServerListener;
};
