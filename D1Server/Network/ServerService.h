#pragma once

#include "Core/CoreMinimal.h"
#include "Iocp/Service.h"
#include "Iocp/Listener.h"
#include "Iocp/NetAddress.h"
#include <utility>

/** 서버 측 Service. */
class ServerService : public Service
{
public:
	
	ServerService(NetAddress InAddress, SessionFactory InFactory)
		: Service(InAddress, std::move(InFactory)) {}

	/** IocpCore 초기화 + Listener 시작. */
	bool Start() override;

	/** Listener 중지 + 모든 세션 정리 + drain 완료까지 block. */
	void Stop() override;

private:
	ListenerRef ServerListener;
};
