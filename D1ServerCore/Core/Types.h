#pragma once

#include <windows.h>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <cassert>

namespace D1
{
	/** 부호 있는 정수 타입 */
	using int8   = int8_t;
	using int16  = int16_t;
	using int32  = int32_t;
	using int64  = int64_t;

	/** 부호 없는 정수 타입 */
	using uint8  = uint8_t;
	using uint16 = uint16_t;
	using uint32 = uint32_t;
	using uint64 = uint64_t;

	/** 문자 타입 */
	using AnsiChar = char;
	using WideChar = wchar_t;
	using Char     = wchar_t;

	/** 플랫폼 의존 크기 타입 */
	using SizeType = size_t;
	using PtrInt   = intptr_t;
	using UPtrInt  = uintptr_t;

	/**
	 * ReadWriteLock 32비트 상태값 비트 레이아웃
	 *
	 * Bit 31:    W 플래그 (Writer 보유 시 1)
	 * Bit 16-30: ThreadID (소유 스레드 ID, 15비트, max 32767)
	 * Bit 0-15:  Count (W=0: ReadCount, W=1: WriteCount/재진입)
	 *
	 * W=1이면 Reader 진입 불가 → RCount는 반드시 0 → 하위 16비트를 WCount로 재활용
	 */
	constexpr uint32 WRITE_FLAG     = 0x80000000;
	constexpr uint32 OWNER_MASK     = 0x7FFF0000;
	constexpr uint32 OWNER_SHIFT    = 16;
	constexpr uint32 COUNT_MASK     = 0x0000FFFF;
	constexpr uint32 MAX_THREAD_ID  = 0x7FFF;

	/** CAS 스핀 횟수. 실패 시 WaitOnAddress로 전환. */
	constexpr uint32 SPIN_COUNT = 512;

	/** 초기화되지 않은 ThreadID 센티넬 값 (ThreadIdCounter는 1부터 시작) */
	constexpr uint32 INVALID_THREAD_ID = 0;

	/** 스마트 포인터 alias */
	class IocpObject;
	using IocpObjectRef = std::shared_ptr<IocpObject>;

	class Session;
	using SessionRef = std::shared_ptr<Session>;

	class PacketSession;
	using PacketSessionRef = std::shared_ptr<PacketSession>;

	class Listener;
	using ListenerRef = std::shared_ptr<Listener>;

	class Service;
	using ServiceRef = std::shared_ptr<Service>;

	class ServerService;
	using ServerServiceRef = std::shared_ptr<ServerService>;

	class ClientService;
	using ClientServiceRef = std::shared_ptr<ClientService>;

	class SendBufferChunk;
	using SendBufferChunkRef = std::shared_ptr<SendBufferChunk>;

	class SendBuffer;
	using SendBufferRef = std::shared_ptr<SendBuffer>;
}

// D1 네임스페이스 타입을 전역으로 노출 (UE 스타일 컨벤션)
using namespace D1;
