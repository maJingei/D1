#include <iostream>
#include <string>
#include <memory>

#include "Iocp/SocketUtils.h"
#include "Iocp/ClientService.h"
#include "Iocp/Session.h"

#include "Threading/ThreadManager.h"
#include "Memory/MemoryPool.h"
#include "Core/Types.h"

using namespace D1;

/**
 * Echo 클라이언트 전용 Session.
 * 서버로부터 수신한 데이터를 콘솔에 출력한다.
 */
class EchoClientSession : public Session
{
public:
	/** 메인 스레드에서 데이터를 전송한다. */
	void SendData(const char* Data, int32 NumOfBytes)
	{
		::memcpy(SendBuffer, Data, NumOfBytes);
		RegisterSend(NumOfBytes);
	}

	bool IsClientConnected() const { return bConnected && !IsDisconnected(); }

protected:
	void OnConnected() override
	{
		bConnected = true;
		std::cout << "[Client] Connected to server!" << std::endl;
		RegisterRecv();
	}

	void OnRecv(int32 NumOfBytes) override
	{
		// 수신 데이터를 콘솔에 출력
		RecvBuffer[NumOfBytes] = '\0';
		std::cout << "[Client] Echo: " << RecvBuffer << std::endl;
		// 다음 수신 등록 (클라이언트는 Echo 재전송 안 함)
		RegisterRecv();
	}

	void OnSend(int32 NumOfBytes) override
	{
		std::cout << "[Client] Sent " << NumOfBytes << " bytes" << std::endl;
	}

	void OnDisconnected() override
	{
		std::cout << "[Client] Server disconnected" << std::endl;
	}

private:
	bool bConnected = false;
};

int main(int argc, char* argv[])
{
	// 초기화
	ThreadManager::InitTLS();
	PoolManager::GetInstance().Initialize(64);
	SocketUtils::Init();

	// ClientService 생성 및 시작
	std::shared_ptr<ClientService> Client = std::make_shared<ClientService>();
	Client->SetSessionFactory([]() -> std::shared_ptr<Session> { return std::make_shared<EchoClientSession>(); });
	Client->Start();

	// 서버 주소 설정 (127.0.0.1:9999)
	SOCKADDR_IN ServerAddr = {};
	ServerAddr.sin_family = AF_INET;
	::inet_pton(AF_INET, "127.0.0.1", &ServerAddr.sin_addr);
	ServerAddr.sin_port = ::htons(9999);

	// 서버에 연결
	std::shared_ptr<Session> ConnectedSession = Client->Connect(ServerAddr);

	// 워커 스레드 1개 생성
	ThreadManager& Manager = ThreadManager::GetInstance();
	Manager.CreateThread([Client]()
	{
		while (Client->GetIocpCore()->Dispatch())
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
	EchoClientSession* ClientSess = static_cast<EchoClientSession*>(ConnectedSession.get());
	std::string Input;
	while (std::getline(std::cin, Input))
	{
		if (Input == "quit") break;
		if (Input.empty()) continue;

		if (ClientSess->IsClientConnected())
		{
			ClientSess->SendData(Input.c_str(), static_cast<int32>(Input.size()));
		}
		else
		{
			std::cout << "[Client] Not connected" << std::endl;
			break;
		}
	}

	// 종료
	::PostQueuedCompletionStatus(Client->GetIocpCore()->GetHandle(), 0, 0, nullptr);
	Manager.JoinAll();

	Client->Stop();
	ConnectedSession.reset();

	SocketUtils::Cleanup();
	PoolManager::GetInstance().Shutdown();
	Manager.DestroyAllThreads();

	std::cout << "[Client] Shutdown complete" << std::endl;

	return 0;
}
