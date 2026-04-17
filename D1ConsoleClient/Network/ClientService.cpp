#include "ClientService.h"
#include "Iocp/Session.h"
#include "Iocp/SocketUtils.h"

SessionRef ClientService::Connect()
{
	SessionRef NewSession = CreateSession();
	if (NewSession == nullptr)
		return nullptr;

	SOCKET Socket = SocketUtils::CreateTcpSocket();
	if (Socket == INVALID_SOCKET)
	{
		ReleaseSession(NewSession);
		return nullptr;
	}

	NewSession->SetSocket(Socket);
	GetIocpCore()->Register(NewSession.get());
	NewSession->RegisterConnect(Address);

	// Connect 등록 이후의 Tick에서 Iocp Dispatch로 EnterGame 패킷 전송
	return NewSession;
}
