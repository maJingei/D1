#pragma once

#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include "Core/CoreMinimal.h"

/**
 * 소켓 유틸리티 static 클래스.
 * WSAStartup/Cleanup, LPFN 런타임 바인딩, 소켓 생성/옵션/Bind/Listen을 제공한다.
 */
class SocketUtils
{
public:
	/** WSAStartup + AcceptEx/ConnectEx/DisconnectEx LPFN 런타임 바인딩 */
	static void Init();

	/** WSACleanup */
	static void Cleanup();

	/*-----------------------------------------------------------------*/
	/*  소켓 생성                                                       */
	/*-----------------------------------------------------------------*/

	/** TCP Overlapped 소켓을 생성한다. */
	static SOCKET CreateTcpSocket();

	/** UDP Overlapped 소켓을 생성한다. */
	static SOCKET CreateUdpSocket();

	/*-----------------------------------------------------------------*/
	/*  소켓 옵션 (공통)                                                */
	/*-----------------------------------------------------------------*/

	static bool SetLinger(SOCKET Socket, uint16 OnOff, uint16 Linger);
	static bool SetReuseAddress(SOCKET Socket, bool bFlag);
	static bool SetRecvBufferSize(SOCKET Socket, int32 Size);
	static bool SetSendBufferSize(SOCKET Socket, int32 Size);

	/*-----------------------------------------------------------------*/
	/*  소켓 옵션 (TCP)                                                 */
	/*-----------------------------------------------------------------*/

	/** Nagle 알고리즘 비활성화 (지연 없는 즉시 전송) */
	static bool SetTcpNoDelay(SOCKET Socket, bool bFlag);

	/** AcceptEx 완료 후 클라이언트 소켓의 컨텍스트를 리슨 소켓과 동기화한다. */
	static bool SetUpdateAcceptSocket(SOCKET Socket, SOCKET ListenSocket);

	/*-----------------------------------------------------------------*/
	/*  소켓 옵션 (UDP)                                                 */
	/*-----------------------------------------------------------------*/

	/** UDP 브로드캐스트 허용 설정 */
	static bool SetBroadcast(SOCKET Socket, bool bFlag);

	/*-----------------------------------------------------------------*/
	/*  Bind / Listen                                                   */
	/*-----------------------------------------------------------------*/

	static bool Bind(SOCKET Socket, SOCKADDR_IN Address);
	static bool BindAnyAddress(SOCKET Socket, uint16 Port);
	static bool Listen(SOCKET Socket, int32 Backlog = SOMAXCONN);

public:
	/** LPFN 함수 포인터 (Init에서 WSAIoctl로 바인딩) */
	static LPFN_CONNECTEX ConnectEx;
	static LPFN_DISCONNECTEX DisconnectEx;
	static LPFN_ACCEPTEX AcceptEx;
};
