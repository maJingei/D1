#pragma once

#include <winsock2.h>
#include "../Core/Types.h"

namespace D1
{
	class IocpEvent;

	/**
	 * IOCP Dispatch를 위한 추상 베이스 클래스.
	 * Listener, Session 등이 상속받아 완료 통지를 처리한다.
	 */
	class IocpObject
	{
	public:
		virtual ~IocpObject() = default;

		/** IOCP에 등록할 핸들을 반환한다. (Session: SOCKET, Listener: 리슨 소켓) */
		virtual HANDLE GetHandle() = 0;

		/**
		 * GQCS 완료 통지를 처리한다.
		 *
		 * @param Event        완료된 IocpEvent
		 * @param NumOfBytes   전송된 바이트 수
		 */
		virtual void Dispatch(IocpEvent* Event, int32 NumOfBytes) = 0;

		IocpObject(const IocpObject&) = delete;
		IocpObject& operator=(const IocpObject&) = delete;
		IocpObject(IocpObject&&) = delete;
		IocpObject& operator=(IocpObject&&) = delete;

	protected:
		IocpObject() = default;
	};
}