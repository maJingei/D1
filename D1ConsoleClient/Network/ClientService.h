#pragma once

#include "Iocp/Service.h"
#include "Iocp/NetAddress.h"
#include <utility>

/** 클라이언트 측 Service. */
class ClientService : public Service
{
public:
	
	ClientService(NetAddress InAddress, SessionFactory InFactory)
		: Service(InAddress, std::move(InFactory)) {}

	/** Session을 생성하고 ConnectEx로 서버에 연결한다. */
	SessionRef Connect();
};