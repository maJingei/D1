#include "SocketUtils.h"

LPFN_CONNECTEX SocketUtils::ConnectEx = nullptr;
LPFN_DISCONNECTEX SocketUtils::DisconnectEx = nullptr;
LPFN_ACCEPTEX SocketUtils::AcceptEx = nullptr;

void SocketUtils::Init()
{
	// Step 1: WSAStartup.
	// Release 빌드에서 assert 는 통째로 제거되므로, 부수효과가 있는 호출은 반드시 변수에 담은 뒤 assert 로 검증한다.
	WSADATA WsaData;
	const int WsaStartupResult = ::WSAStartup(MAKEWORD(2, 2), &WsaData);
	assert(WsaStartupResult == 0);
	(void)WsaStartupResult;

	// Step 2: 임시 소켓으로 LPFN 바인딩
	SOCKET DummySocket = CreateTcpSocket();
	assert(DummySocket != INVALID_SOCKET);

	DWORD Bytes = 0;

	// AcceptEx 바인딩
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	const int AcceptExResult = ::WSAIoctl(DummySocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &AcceptEx, sizeof(AcceptEx), &Bytes, nullptr, nullptr);
	assert(AcceptExResult != SOCKET_ERROR);
	(void)AcceptExResult;

	// ConnectEx 바인딩
	GUID GuidConnectEx = WSAID_CONNECTEX;
	const int ConnectExResult = ::WSAIoctl(DummySocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx, sizeof(GuidConnectEx), &ConnectEx, sizeof(ConnectEx), &Bytes, nullptr, nullptr);
	assert(ConnectExResult != SOCKET_ERROR);
	(void)ConnectExResult;

	// DisconnectEx 바인딩
	GUID GuidDisconnectEx = WSAID_DISCONNECTEX;
	const int DisconnectExResult = ::WSAIoctl(DummySocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidDisconnectEx, sizeof(GuidDisconnectEx), &DisconnectEx, sizeof(DisconnectEx), &Bytes, nullptr, nullptr);
	assert(DisconnectExResult != SOCKET_ERROR);
	(void)DisconnectExResult;

	::closesocket(DummySocket);
}

void SocketUtils::Cleanup()
{
	::WSACleanup();
}

/*-----------------------------------------------------------------*/
/*  소켓 생성                                                       */
/*-----------------------------------------------------------------*/

SOCKET SocketUtils::CreateTcpSocket()
{
	return ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

SOCKET SocketUtils::CreateUdpSocket()
{
	return ::WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

/*-----------------------------------------------------------------*/
/*  소켓 옵션 (공통)                                                */
/*-----------------------------------------------------------------*/

bool SocketUtils::SetLinger(SOCKET Socket, uint16 OnOff, uint16 Linger)
{
	LINGER Option = { OnOff, Linger };
	return ::setsockopt(Socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&Option), sizeof(Option)) != SOCKET_ERROR;
}

bool SocketUtils::SetReuseAddress(SOCKET Socket, bool bFlag)
{
	BOOL Option = bFlag ? TRUE : FALSE;
	return ::setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&Option), sizeof(Option)) != SOCKET_ERROR;
}

bool SocketUtils::SetRecvBufferSize(SOCKET Socket, int32 Size)
{
	return ::setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&Size), sizeof(Size)) != SOCKET_ERROR;
}

bool SocketUtils::SetSendBufferSize(SOCKET Socket, int32 Size)
{
	return ::setsockopt(Socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&Size), sizeof(Size)) != SOCKET_ERROR;
}

/*-----------------------------------------------------------------*/
/*  소켓 옵션 (TCP)                                                 */
/*-----------------------------------------------------------------*/

bool SocketUtils::SetTcpNoDelay(SOCKET Socket, bool bFlag)
{
	BOOL Option = bFlag ? TRUE : FALSE;
	return ::setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&Option), sizeof(Option)) != SOCKET_ERROR;
}

bool SocketUtils::SetUpdateAcceptSocket(SOCKET Socket, SOCKET ListenSocket)
{
	return ::setsockopt(Socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char*>(&ListenSocket), sizeof(ListenSocket)) != SOCKET_ERROR;
}

/*-----------------------------------------------------------------*/
/*  소켓 옵션 (UDP)                                                 */
/*-----------------------------------------------------------------*/

bool SocketUtils::SetBroadcast(SOCKET Socket, bool bFlag)
{
	BOOL Option = bFlag ? TRUE : FALSE;
	return ::setsockopt(Socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&Option), sizeof(Option)) != SOCKET_ERROR;
}

/*-----------------------------------------------------------------*/
/*  Bind / Listen                                                   */
/*-----------------------------------------------------------------*/

bool SocketUtils::Bind(SOCKET Socket, SOCKADDR_IN Address)
{
	return ::bind(Socket, reinterpret_cast<const SOCKADDR*>(&Address), sizeof(Address)) != SOCKET_ERROR;
}

bool SocketUtils::BindAnyAddress(SOCKET Socket, uint16 Port)
{
	SOCKADDR_IN Address = {};
	Address.sin_family = AF_INET;
	Address.sin_addr.s_addr = ::htonl(INADDR_ANY);
	Address.sin_port = ::htons(Port);
	return Bind(Socket, Address);
}

bool SocketUtils::Listen(SOCKET Socket, int32 Backlog)
{
	return ::listen(Socket, Backlog) != SOCKET_ERROR;
}
