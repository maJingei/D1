#include "ClientService.h"
#include "Session.h"
#include "SocketUtils.h"
#include <iostream>

namespace D1
{
	std::shared_ptr<Session> ClientService::Connect(const SOCKADDR_IN& Address)
	{
		std::shared_ptr<Session> NewSession = CreateSession();
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