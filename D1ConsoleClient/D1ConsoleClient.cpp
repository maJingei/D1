#include <iostream>
#include <string>

#include "Iocp/SocketUtils.h"
#include "Iocp/IocpCore.h"
#include "Iocp/IocpEvent.h"
#include "Iocp/IocpObject.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Core/Types.h"

using namespace D1;

/**
 * Echo 클라이언트 전용 IocpObject.
 * Session과 유사하지만, ProcessRecv에서 Echo 대신 콘솔 출력을 수행한다.
 */
class ClientSession : public IocpObject
{
public:
	ClientSession()
	{
		RecvIocpEvent.Owner = this;
		SendIocpEvent.Owner = this;
		ConnectIocpEvent.Owner = this;
	}

	~ClientSession()
	{
		if (Socket != INVALID_SOCKET)
		{
			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}
	}

	HANDLE GetHandle() override { return reinterpret_cast<HANDLE>(Socket); }

	void Dispatch(IocpEvent* Event, int32 NumOfBytes) override
	{
		switch (Event->Type)
		{
		case EventType::Connect: ProcessConnect(); break;
		case EventType::Recv:    ProcessRecv(NumOfBytes); break;
		case EventType::Send:    ProcessSend(NumOfBytes); break;
		default: break;
		}
	}

	void SetSocket(SOCKET InSocket) { Socket = InSocket; }
	SOCKET GetSocket() const { return Socket; }

	void RegisterConnect(const SOCKADDR_IN& Address)
	{
		ConnectIocpEvent.Init();
		SocketUtils::BindAnyAddress(Socket, 0);

		DWORD Bytes = 0;
		BOOL Result = SocketUtils::ConnectEx(Socket, reinterpret_cast<const SOCKADDR*>(&Address), sizeof(Address), nullptr, 0, &Bytes, &ConnectIocpEvent);
		if (Result == FALSE && ::WSAGetLastError() != WSA_IO_PENDING)
			std::cout << "[Client] ConnectEx failed: " << ::WSAGetLastError() << std::endl;
	}

	void RegisterRecv()
	{
		if (bDisconnected) return;

		RecvIocpEvent.Init();

		WSABUF WsaBuf;
		WsaBuf.buf = RecvBuffer;
		WsaBuf.len = sizeof(RecvBuffer);
		DWORD Flags = 0;

		int32 Result = ::WSARecv(Socket, &WsaBuf, 1, nullptr, &Flags, &RecvIocpEvent, nullptr);
		if (Result == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
			std::cout << "[Client] WSARecv failed: " << ::WSAGetLastError() << std::endl;
	}

	void RegisterSend(const char* Data, int32 NumOfBytes)
	{
		if (bDisconnected) return;

		SendIocpEvent.Init();

		::memcpy(SendBuffer, Data, NumOfBytes);

		WSABUF WsaBuf;
		WsaBuf.buf = SendBuffer;
		WsaBuf.len = NumOfBytes;

		int32 Result = ::WSASend(Socket, &WsaBuf, 1, nullptr, 0, &SendIocpEvent, nullptr);
		if (Result == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
			std::cout << "[Client] WSASend failed: " << ::WSAGetLastError() << std::endl;
	}

	void ProcessConnect()
	{
		std::cout << "[Client] Connected to server!" << std::endl;
		bConnected = true;
		RegisterRecv();
	}

	void ProcessRecv(int32 NumOfBytes)
	{
		if (bDisconnected) return;

		if (NumOfBytes == 0)
		{
			bDisconnected = true;
			std::cout << "[Client] Server disconnected" << std::endl;
			return;
		}

		// 수신 데이터를 콘솔에 출력
		RecvBuffer[NumOfBytes] = '\0';
		std::cout << "[Client] Echo: " << RecvBuffer << std::endl;

		// 다음 수신 등록 (클라이언트는 Echo 재전송 안 함)
		RegisterRecv();
	}

	void ProcessSend(int32 NumOfBytes)
	{
		if (bDisconnected) return;
		std::cout << "[Client] Sent " << NumOfBytes << " bytes" << std::endl;
	}

	bool IsConnected() const { return bConnected && !bDisconnected; }

private:
	SOCKET Socket = INVALID_SOCKET;
	bool bConnected = false;
	bool bDisconnected = false;

	RecvEvent RecvIocpEvent;
	SendEvent SendIocpEvent;
	ConnectEvent ConnectIocpEvent;

	char RecvBuffer[4096] = {};
	char SendBuffer[4096] = {};
};

int main(int argc, char* argv[])
{
	// 초기화
	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);
	SocketUtils::Init();

	IocpCore Core;
	Core.Initialize();

	// 클라이언트 세션 생성
	ClientSession Client;
	SOCKET Socket = SocketUtils::CreateTcpSocket();
	Client.SetSocket(Socket);
	Core.Register(&Client);

	// 서버 주소 설정 (127.0.0.1:9999)
	SOCKADDR_IN ServerAddr = {};
	ServerAddr.sin_family = AF_INET;
	::inet_pton(AF_INET, "127.0.0.1", &ServerAddr.sin_addr);
	ServerAddr.sin_port = ::htons(9999);

	// ConnectEx로 서버 연결
	Client.RegisterConnect(ServerAddr);

	// 워커 스레드 1개 생성
	ThreadManager& Manager = ThreadManager::GetInstance();
	Manager.CreateThread([&Core]()
	{
		while (Core.Dispatch())
		{
		}
	});
	Manager.Launch();

	std::cout << "========================================" << std::endl;
	std::cout << "  D1ConsoleClient (Echo Test)" << std::endl;
	std::cout << "  Type message and press Enter to send" << std::endl;
	std::cout << "  Type 'quit' to exit" << std::endl;
	std::cout << "========================================" << std::endl;

	// 메인 스레드: 사용자 입력 → Send
	std::string Input;
	while (std::getline(std::cin, Input))
	{
		if (Input == "quit") break;
		if (Input.empty()) continue;

		if (Client.IsConnected())
		{
			Client.RegisterSend(Input.c_str(), static_cast<int32>(Input.size()));
		}
		else
		{
			std::cout << "[Client] Not connected" << std::endl;
			break;
		}
	}

	// 종료
	::PostQueuedCompletionStatus(Core.GetHandle(), 0, 0, nullptr);
	Manager.JoinAll();

	SocketUtils::Cleanup();
	PoolManager::GetInstance().Shutdown();
	Manager.DestroyAllThreads();

	std::cout << "[Client] Shutdown complete" << std::endl;

	return 0;
}