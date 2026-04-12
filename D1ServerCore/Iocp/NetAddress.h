#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include "../Core/Types.h"

namespace D1
{
	/**
	 * IPv4 소켓 주소(SOCKADDR_IN)를 래핑하는 value-like 클래스.
	 * Listener/Session/Service의 주소 인터페이스에서 SOCKADDR_IN을 직접 노출하지 않고
	 * 의미 있는 접근자(GetIp/GetPort/GetSockAddr)를 통해 사용하기 위한 래퍼다.
	 *
	 * 불변이 아니라 복사·대입 가능한 value 객체로 사용된다.
	 * 현재는 IPv4 전용. (IPv6 지원은 NetAddress6/또는 조건 분기로 추후 확장)
	 */
	class NetAddress
	{
	public:
		NetAddress() = default;

		/** SOCKADDR_IN으로부터 생성한다. (AcceptEx 결과 파싱 등) */
		explicit NetAddress(const SOCKADDR_IN& InSockAddr) : SockAddr(InSockAddr) {}

		/**
		 * IP 문자열 + 포트로부터 생성한다.
		 *
		 * @param Ip    점 표기 IPv4 문자열 (예: "127.0.0.1")
		 * @param Port  호스트 바이트 순서 포트 번호
		 */
		NetAddress(const std::string& Ip, uint16 Port);

		/** INADDR_ANY + 지정 포트로 생성한다. (서버 바인드용) */
		static NetAddress AnyAddress(uint16 Port);

		/*-----------------------------------------------------------------*/
		/*  접근자                                                          */
		/*-----------------------------------------------------------------*/

		/** 내부 SOCKADDR_IN 참조를 반환한다. (Bind/Connect 등에 전달) */
		const SOCKADDR_IN& GetSockAddr() const { return SockAddr; }
		SOCKADDR_IN& GetSockAddrRef() { return SockAddr; }

		/** 점 표기 IPv4 문자열을 반환한다. */
		std::string GetIp() const;

		/** 호스트 바이트 순서 포트 번호를 반환한다. */
		uint16 GetPort() const { return ::ntohs(SockAddr.sin_port); }

	private:
		SOCKADDR_IN SockAddr = {};
	};
}