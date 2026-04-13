#include "Session.h"
#include "Service.h"
#include "SocketUtils.h"
#include <iostream>
#include <vector>

namespace D1
{
	Session::Session() = default;

	Session::~Session()
	{
		if (Socket != INVALID_SOCKET)
		{
			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}
	}

	HANDLE Session::GetHandle()
	{
		return reinterpret_cast<HANDLE>(Socket);
	}

	void Session::Dispatch(IocpEvent* Event, int32 NumOfBytes)
	{
		switch (Event->Type)
		{
		case EventType::Connect:    ProcessConnect(); break;
		case EventType::Disconnect: ProcessDisconnect(); break;
		case EventType::Recv:       ProcessRecv(NumOfBytes); break;
		case EventType::Send:       ProcessSend(NumOfBytes); break;
		default: break;
		}
	}

	/*-----------------------------------------------------------------*/
	/*  I/O 등록                                                        */
	/*-----------------------------------------------------------------*/

	void Session::RegisterRecv()
	{
		if (bDisconnected) return;

		HoldForIo(RecvIocpEvent);

		// 수신 전 공간 확보: ReadPos==WritePos면 리셋, 잔여 있으면 앞으로 memmove
		Recv.Clean();

		WSABUF WsaBuf;
		WsaBuf.buf = reinterpret_cast<char*>(Recv.WritePtr());
		WsaBuf.len = static_cast<ULONG>(Recv.GetFreeSize());

		DWORD Flags = 0;
		int32 Result = ::WSARecv(Socket, &WsaBuf, 1, nullptr, &Flags, &RecvIocpEvent, nullptr);
		if (Result == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Session] WSARecv failed: " << ::WSAGetLastError() << std::endl;
			// 게시 실패: HoldForIo가 잡아둔 self-ref를 즉시 해제한다.
			RecvIocpEvent.Owner.reset();
		}
	}

	void Session::Send(SendBufferRef InSendBuffer)
	{
		if (bDisconnected || InSendBuffer == nullptr) return;

		// 큐에 적재하고, 현재 WSASend가 진행 중이 아닌 경우에만 한 번 트리거한다.
		// 이후 추가 Send 호출은 플래그가 true인 동안 큐에 쌓이기만 하고,
		// 전송 완료 시 ProcessSend가 남은 큐를 한 번의 WSASend로 묶어 flush한다.
		bool ShouldRegisterSend = false;
		{
			WriteLockGuard Lock(SendLock);
			SendQueue.push(std::move(InSendBuffer));
			if (bSendRegistered == false)
			{
				bSendRegistered = true;
				ShouldRegisterSend = true;
			}
		}

		if (ShouldRegisterSend)
		{
			RegisterSend();
		}
	}

	void Session::RegisterSend()
	{
		if (bDisconnected) return;

		HoldForIo(SendIocpEvent);

		// Step 1: 현재 큐의 모든 SendBuffer를 꺼내 SendEvent가 보관한다.
		//         (shared_ptr 참조로 WSASend 진행 동안 Chunk 메모리 수명 보장)
		{
			WriteLockGuard Lock(SendLock);
			while (SendQueue.empty() == false)
			{
				SendIocpEvent.SendBuffers.push_back(std::move(SendQueue.front()));
				SendQueue.pop();
			}
		}

		// Step 2: WSABUF 배열로 구성 → 한 번의 WSASend로 Scatter-Gather 전송
		std::vector<WSABUF> WsaBufs;
		WsaBufs.reserve(SendIocpEvent.SendBuffers.size());
		for (const SendBufferRef& Buf : SendIocpEvent.SendBuffers)
		{
			WSABUF W;
			W.buf = reinterpret_cast<char*>(Buf->Buffer());
			W.len = static_cast<ULONG>(Buf->GetWriteSize());
			WsaBufs.push_back(W);
		}

		DWORD NumOfBytes = 0;
		int32 Result = ::WSASend(Socket, WsaBufs.data(), static_cast<DWORD>(WsaBufs.size()), &NumOfBytes, 0, &SendIocpEvent, nullptr);
		if (Result == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Session] WSASend failed: " << ::WSAGetLastError() << std::endl;
			// 실패 시 이벤트에 보관된 버퍼들 해제 + 플래그 롤백
			SendIocpEvent.SendBuffers.clear();
			// 게시 실패: HoldForIo가 잡아둔 self-ref를 즉시 해제한다.
			SendIocpEvent.Owner.reset();
			WriteLockGuard Lock(SendLock);
			bSendRegistered = false;
		}
	}

	void Session::RegisterConnect(const NetAddress& Address)
	{
		if (bDisconnected) return;

		HoldForIo(ConnectIocpEvent);

		// ConnectEx는 사전 bind 필수
		SocketUtils::BindAnyAddress(Socket, 0);

		const SOCKADDR_IN& SockAddr = Address.GetSockAddr();
		DWORD Bytes = 0;
		BOOL Result = SocketUtils::ConnectEx(Socket, reinterpret_cast<const SOCKADDR*>(&SockAddr), sizeof(SockAddr), nullptr, 0, &Bytes, &ConnectIocpEvent);
		if (Result == FALSE && ::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "[Session] ConnectEx failed: " << ::WSAGetLastError() << std::endl;
			// 게시 실패: HoldForIo가 잡아둔 self-ref를 즉시 해제한다.
			ConnectIocpEvent.Owner.reset();
		}
	}

	/*-----------------------------------------------------------------*/
	/*  이벤트 처리 (내부)                                              */
	/*-----------------------------------------------------------------*/

	void Session::ProcessConnect()
	{
		if (bDisconnected) return;
		std::cout << "[Session] Connected" << std::endl;
		OnConnected();
	}

	void Session::ProcessDisconnect()
	{
		if (bDisconnected) return;
		bDisconnected = true;

		// ReleaseSession에서 Sessions에서 제거되어도 이 함수가 끝날 때까지 수명 보장.
		// shared_from_this()는 베이스 IocpObject가 제공하므로 Session 타입으로 downcast 필요.
		SessionRef Self = std::static_pointer_cast<Session>(shared_from_this());

		if (Socket != INVALID_SOCKET)
		{
			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}
		OnDisconnected();
		if (ServiceRef Owner = OwnerService.lock())
		{
			Owner->ReleaseSession(Self);
		}
	}

	void Session::ProcessRecv(int32 NumOfBytes)
	{
		if (bDisconnected) return;
		// 연결 종료 감지
		if (NumOfBytes == 0)
		{
			ProcessDisconnect();
			return;
		}

		// 이번 WSARecv로 수신된 바이트를 WritePos에 반영
		if (Recv.OnWrite(NumOfBytes) == false)
		{
			std::cout << "[Session] Recv OnWrite overflow (NumOfBytes=" << NumOfBytes << ", free=" << Recv.GetFreeSize() << ")" << std::endl;
			ProcessDisconnect();
			return;
		}

		// 파생 OnRecv에 누적된 모든 미처리 바이트를 넘긴다
		const int32 DataSize = Recv.GetDataSize();
		const int32 Processed = OnRecv(Recv.ReadPtr(), DataSize);

		if (Processed < 0 || Processed > DataSize || Recv.OnRead(Processed) == false)
		{
			std::cout << "[Session] OnRecv processed invalid bytes (" << Processed << "/" << DataSize << ")" << std::endl;
			ProcessDisconnect();
			return;
		}

		// 다음 수신 대기. Send 경로는 별개로 Scatter-Gather로 진행되므로 여기서 즉시 재등록한다.
		RegisterRecv();
	}

	void Session::ProcessSend(int32 NumOfBytes)
	{
		// 전송 완료된 SendBuffer들을 먼저 해제한다 (Chunk 참조 감소).
		SendIocpEvent.SendBuffers.clear();

		if (bDisconnected) return;
		if (NumOfBytes == 0)
		{
			ProcessDisconnect();
			return;
		}

		OnSend(NumOfBytes);

		// 전송 중에 새로 쌓인 SendBuffer가 있으면 다시 RegisterSend,
		// 없으면 bSendRegistered를 내려 다음 Send가 트리거를 걸 수 있게 한다.
		bool ShouldRegisterSend = false;
		{
			WriteLockGuard Lock(SendLock);
			if (SendQueue.empty())
			{
				bSendRegistered = false;
			}
			else
			{
				ShouldRegisterSend = true;
			}
		}

		if (ShouldRegisterSend)
		{
			RegisterSend();
		}
	}

	/*-----------------------------------------------------------------*/
	/*  가상 훅 기본 구현                                               */
	/*-----------------------------------------------------------------*/

	void Session::OnConnected()
	{
		RegisterRecv();
	}

	int32 Session::OnRecv(uint8* Data, int32 NumOfBytes)
	{
		// Echo 기본 동작: 누적된 바이트를 Chunk 크기 이하 단위로 쪼개어 Send.
		// 한 번의 WSASend는 SendQueue에 쌓인 모든 버퍼를 Scatter-Gather로 묶어 전송한다.
		int32 Remaining = NumOfBytes;
		int32 Offset    = 0;
		while (Remaining > 0)
		{
			const int32 BytesToSend = Remaining < SendBufferChunk::ChunkSize ? Remaining : SendBufferChunk::ChunkSize;

			SendBufferRef Buf = SendBufferManager::Get().Open(BytesToSend);
			::memcpy(Buf->Buffer(), Data + Offset, static_cast<size_t>(BytesToSend));
			Buf->Close(BytesToSend);

			Send(Buf);

			Offset    += BytesToSend;
			Remaining -= BytesToSend;
		}
		return NumOfBytes;
	}

	void Session::OnSend(int32 NumOfBytes)
	{
		std::cout << "[Session] Sent " << NumOfBytes << " bytes" << std::endl;
	}

	void Session::OnDisconnected()
	{
		std::cout << "[Session] Disconnected" << std::endl;
	}

	/*-----------------------------------------------------------------*/
	/*  소켓 관리                                                       */
	/*-----------------------------------------------------------------*/

	void Session::SetSocket(SOCKET InSocket)
	{
		Socket = InSocket;
	}

	void Session::SetOwnerService(std::weak_ptr<Service> InService)
	{
		OwnerService = InService;
	}
}
