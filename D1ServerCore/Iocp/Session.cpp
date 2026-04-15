#include "Session.h"
#include "Service.h"
#include "SocketUtils.h"
#include "../Core/DiagCounters.h"
#include "../Job/Job.h"
#include <iostream>
#include <vector>

namespace D1
{
	Session::Session()
	{
		// Serializer 는 Session 이 shared_ptr 로 생성된 직후 InitSerializer 에서 초기화한다.
		// 생성자 시점에는 shared_from_this() 를 아직 호출할 수 없으므로 여기서는 생성하지 않는다.
	}

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

		// Serializer 가 아직 초기화되지 않았으면 지금 생성한다.
		// RegisterRecv 는 OnConnected 에서 최초 호출되며, 그 시점에는 shared_ptr 이 확정됨.
		if (Serializer == nullptr)
		{
			SessionRef Self = std::static_pointer_cast<Session>(shared_from_this());
			Serializer = std::make_shared<FSessionSerializer>(Self);
		}

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
			RecvIocpEvent.Owner.reset();
		}
	}

	void Session::Send(SendBufferRef InSendBuffer)
	{
		if (bDisconnected || InSendBuffer == nullptr) return;

		// Serializer 가 아직 초기화되지 않았으면 지금 생성한다.
		// (RegisterRecv 이전에 Send 가 호출될 수 있는 경로 대비)
		if (Serializer == nullptr)
		{
			SessionRef Self = std::static_pointer_cast<Session>(shared_from_this());
			Serializer = std::make_shared<FSessionSerializer>(Self);
		}

		// DoSend 를 Job 으로 래핑하여 Serializer 에 Push 한다.
		// 같은 세션에 대한 Send 호출 순서가 FIFO 로 보장된다.
		std::shared_ptr<FSessionSerializer> S = Serializer;
		Serializer->PushJob(std::make_shared<Job>([this, S, InSendBuffer]()
		{
			DoSend(InSendBuffer);
		}));
	}

	void Session::RegisterSend()
	{
		if (bDisconnected) return;

		HoldForIo(SendIocpEvent);

		// Step 1: 현재 큐의 모든 SendBuffer를 꺼내 SendEvent가 보관한다.
		//         Serializer 직렬화 안에서만 호출되므로 락 불필요.
		while (SendQueue.empty() == false)
		{
			SendIocpEvent.SendBuffers.push_back(std::move(SendQueue.front()));
			SendQueue.pop();
		}

		// [DIAG] probe: WSASend 한 건당 배치 크기 (SendBuffers 확정 직후)
		GSendBatchSizeSum.fetch_add(static_cast<uint64>(SendIocpEvent.SendBuffers.size()), std::memory_order_relaxed);
		GSendBatchSizeCount.fetch_add(1, std::memory_order_relaxed);

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
			SendIocpEvent.SendBuffers.clear();
			SendIocpEvent.Owner.reset();
			// WSASend 실패 시 플래그를 내려 다음 Send 가 재트리거할 수 있게 한다.
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
		if (NumOfBytes == 0)
		{
			ProcessDisconnect();
			return;
		}

		if (Recv.OnWrite(NumOfBytes) == false)
		{
			std::cout << "[Session] Recv OnWrite overflow (NumOfBytes=" << NumOfBytes << ", free=" << Recv.GetFreeSize() << ")" << std::endl;
			ProcessDisconnect();
			return;
		}

		const int32 DataSize = Recv.GetDataSize();
		const int32 Processed = OnRecv(Recv.ReadPtr(), DataSize);

		if (Processed < 0 || Processed > DataSize || Recv.OnRead(Processed) == false)
		{
			std::cout << "[Session] OnRecv processed invalid bytes (" << Processed << "/" << DataSize << ")" << std::endl;
			ProcessDisconnect();
			return;
		}

		RegisterRecv();
	}

	void Session::ProcessSend(int32 NumOfBytes)
	{
		// 전송 완료된 SendBuffer들을 먼저 해제한다 (Chunk 참조 감소).
		// 이 작업은 IOCP 워커에서 직접 수행해도 안전하다 (SendBuffers 는 이 시점에 전용).
		SendIocpEvent.SendBuffers.clear();

		if (bDisconnected) return;
		if (NumOfBytes == 0)
		{
			ProcessDisconnect();
			return;
		}

		// DoProcessSend 를 Job 으로 래핑하여 Serializer 에 Push 한다.
		// DoSend 와 같은 직렬화 파이프라인에서 실행되므로 SendQueue/bSendRegistered 경쟁 없음.
		if (Serializer == nullptr) return;
		Serializer->PushJob(std::make_shared<Job>([this, NumOfBytes]()
		{
			DoProcessSend(NumOfBytes);
		}));
	}

	/*-----------------------------------------------------------------*/
	/*  Send 직렬화 내부 메서드 (FlushJob 컨텍스트 전용)               */
	/*-----------------------------------------------------------------*/

	void Session::DoSend(SendBufferRef InSendBuffer)
	{
		// [DIAG] probe: Send 진입 경로 판단은 Job 실행 시점(DoSend) 기준.
		SendQueue.push(std::move(InSendBuffer));
		if (bSendRegistered == false)
		{
			bSendRegistered = true;
			// [DIAG] probe: WSASend 트리거 경로 (RegisterSend 예정)
			GSendRegisterCount.fetch_add(1, std::memory_order_relaxed);
			RegisterSend();
		}
		else
		{
			// [DIAG] probe: 이미 WSASend 진행 중 — 큐에 적재만
			GSendAppendCount.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void Session::DoProcessSend(int32 NumOfBytes)
	{
		OnSend(NumOfBytes);

		// 전송 중에 새로 쌓인 SendBuffer 가 있으면 다시 RegisterSend,
		// 없으면 bSendRegistered 를 내려 다음 DoSend 가 트리거를 걸 수 있게 한다.
		if (SendQueue.empty())
		{
			bSendRegistered = false;
		}
		else
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
		int32 Remaining = NumOfBytes;
		int32 Offset = 0;
		while (Remaining > 0)
		{
			const int32 BytesToSend = Remaining < SendBufferChunk::ChunkSize ? Remaining : SendBufferChunk::ChunkSize;

			SendBufferRef Buf = SendBufferManager::Get().Open(BytesToSend);
			::memcpy(Buf->Buffer(), Data + Offset, static_cast<size_t>(BytesToSend));
			Buf->Close(BytesToSend);

			Send(Buf);

			Offset += BytesToSend;
			Remaining -= BytesToSend;
		}
		return NumOfBytes;
	}

	void Session::OnSend(int32 NumOfBytes)
	{
		// std::cout << "[Session] Sent " << NumOfBytes << " bytes" << std::endl;
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