#pragma once

#include "Iocp/Service.h"
#include "Iocp/NetAddress.h"
#include <utility>

/**
 * 클라이언트 측 Service.
 * SessionFactory로 Session을 생성하고 서버에 연결한다.
 */
class ClientService : public Service
{
public:
	/**
	 * @param InAddress  연결할 서버 주소
	 * @param InFactory  Session 생성 팩토리
	 */
	ClientService(NetAddress InAddress, SessionFactory InFactory)
		: Service(InAddress, std::move(InFactory)) {}

	/**
	 * Session을 생성하고 ConnectEx로 서버에 연결한다. (멤버 Address 사용)
	 *
	 * @return 생성된 Session (실패 시 nullptr)
	 */
	SessionRef Connect();
};