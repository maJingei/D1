#include "Iocp/Session.h"
#include "Iocp/Service.h"
#include "Iocp/SocketUtils.h"
#include "Iocp/IocpCore.h"
#include "Core/DiagCounters.h"
#include "Job/Job.h"
#include <iostream>
#include <vector>

Session::Session() : SendSerializer(std::make_shared<JobQueue>())
{
	// SendSerializer 는 세션 생성 즉시 make_shared 로 초기화.
	// 세션 수명 내내 동일한 shared_ptr<JobQueue> 를 유지한다.
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
	if (bDisconnected.load(std::memory_order_relaxed)) return;

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
	if (bDisconnected.load(std::memory_order_relaxed) || InSendBuffer == nullptr) return;

	GSendPacketCount.fetch_add(1, std::memory_order_relaxed); // LOG LOGIC : 서버 송신 패킷 1건 (broadcast 시 N명 분량 모두 카운트됨)

	// DoSend 를 Job 으로 래핑하여 SendSerializer 에 Push 한다.
	// 같은 세션의 Send 순서는 FIFO 로 보장되며, 서로 다른 세션의 Send 는 FlushWorker 풀에 분산 실행된다.
	SendSerializer->PushJob(std::make_shared<Job>([this, InSendBuffer]()
	{
		DoSend(InSendBuffer);
	}));
}

void Session::RegisterSend()
{
	if (bDisconnected.load(std::memory_order_relaxed)) return;

	HoldForIo(SendIocpEvent);

	// 현재 큐의 모든 SendBuffer 를 꺼내 SendEvent 가 보관한다.
	// SendSerializer 직렬화 안에서만 호출되므로 락 불필요.
	while (SendQueue.empty() == false)
	{
		SendIocpEvent.SendBuffers.push_back(std::move(SendQueue.front()));
		SendQueue.pop();
	}

	// [DIAG] probe: WSASend 한 건당 배치 크기
	GSendBatchSizeSum.fetch_add(static_cast<uint64>(SendIocpEvent.SendBuffers.size()), std::memory_order_relaxed);
	GSendBatchSizeCount.fetch_add(1, std::memory_order_relaxed);

	// WSABUF 배열 구성 → 한 번의 WSASend 로 Scatter-Gather 전송
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
		const int32 LastErr = ::WSAGetLastError();
		SendIocpEvent.SendBuffers.clear();
		SendIocpEvent.Owner.reset();
		bSendRegistered = false;

		// 에러 종류와 무관하게 세션당 최초 1회만 로그 + PQCS teardown 발사.
		// 동일 세션의 후속 Job 은 bDisconnected 체크로 조용히 스킵된다.
		// bDisconnected.exchange 로 CAS 하여 로그 폭주와 PQCS 중복 발사를 한 번에 막는다.
		if (bDisconnected.exchange(true, std::memory_order_acq_rel) == false)
		{
			std::cout << "[Session] WSASend failed: " << LastErr << std::endl;
			if (ServiceRef Owner = OwnerService.lock())
			{
				HoldForIo(DisconnectIocpEvent);
				::PostQueuedCompletionStatus(Owner->GetIocpCore()->GetHandle(), 0, 0, &DisconnectIocpEvent);
			}
		}
	}
}

void Session::RegisterConnect(const NetAddress& Address)
{
	if (bDisconnected.load(std::memory_order_relaxed)) return;

	HoldForIo(ConnectIocpEvent);

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
	if (bDisconnected.load(std::memory_order_relaxed)) return;
	std::cout << "[Session] Connected" << std::endl;
	OnConnected();
}

void Session::ProcessDisconnect()
{
	// 테어다운 1회 보장 — 정상 recv-EOF / abort-drain / RegisterSend 에러 PQCS 세 경로 모두 여기로 모이되
	// 실제 소켓 close / OnDisconnected / ReleaseSession 은 CAS 에 성공한 첫 호출만 수행한다.
	bool bExpected = false;
	if (bTeardownDone.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel) == false)
		return;

	bDisconnected.store(true, std::memory_order_release);

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
	if (bDisconnected.load(std::memory_order_relaxed)) return;
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
	// 전송 완료된 SendBuffer 들을 먼저 해제한다 (Chunk 참조 감소). 이 시점 SendBuffers 는 본 워커 전용.
	SendIocpEvent.SendBuffers.clear();

	if (bDisconnected.load(std::memory_order_relaxed)) return;
	if (NumOfBytes == 0)
	{
		ProcessDisconnect();
		return;
	}

	// DoProcessSend 를 Job 으로 래핑해 SendSerializer 에 Push.
	// DoSend 와 같은 직렬화 파이프라인에서 실행되어 SendQueue/bSendRegistered 경쟁 없음.
	SendSerializer->PushJob(std::make_shared<Job>([this, NumOfBytes]()
	{
		DoProcessSend(NumOfBytes);
	}));
}

/*-----------------------------------------------------------------*/
/*  Send 직렬화 내부 메서드 (FlushJob 컨텍스트 전용)               */
/*-----------------------------------------------------------------*/

void Session::DoSend(SendBufferRef InSendBuffer)
{
	SendQueue.push(std::move(InSendBuffer));
	if (bSendRegistered == false)
	{
		bSendRegistered = true;
		// [DIAG] probe: WSASend 트리거 경로
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