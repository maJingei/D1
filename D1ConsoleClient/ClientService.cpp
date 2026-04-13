#include "ClientService.h"
#include "Iocp/Session.h"
#include "Iocp/SocketUtils.h"
#include <iostream>

namespace D1
{
	SessionRef ClientService::Connect(const NetAddress& Address)
	{
		SessionRef NewSession = CreateSession();
		if (NewSession == nullptr)
			return nullptr;

		SOCKET Socket = SocketUtils::CreateTcpSocket();
		if (Socket == INVALID_SOCKET)
		{
			std::cout << "[ClientService] CreateTcpSocket failed" << std::endl;
			ReleaseSession(NewSession);
			return nullptr;
		}

		NewSession->SetSocket(Socket);
		GetIocpCore()->Register(NewSession.get());
		NewSession->RegisterConnect(Address);

		std::cout << "[ClientService] Connecting to server..." << std::endl;
		return NewSession;
	}
}
