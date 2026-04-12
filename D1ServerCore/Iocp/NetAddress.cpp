#include "NetAddress.h"

namespace D1
{
	NetAddress::NetAddress(const std::string& Ip, uint16 Port)
	{
		SockAddr.sin_family = AF_INET;
		SockAddr.sin_port = ::htons(Port);
		// InetPtonA: 점 표기 IPv4 문자열을 32비트 네트워크 바이트 순서로 변환한다
		::InetPtonA(AF_INET, Ip.c_str(), &SockAddr.sin_addr);
	}

	NetAddress NetAddress::AnyAddress(uint16 Port)
	{
		NetAddress Result;
		Result.SockAddr.sin_family = AF_INET;
		Result.SockAddr.sin_port = ::htons(Port);
		Result.SockAddr.sin_addr.s_addr = ::htonl(INADDR_ANY);
		return Result;
	}

	std::string NetAddress::GetIp() const
	{
		char Buffer[INET_ADDRSTRLEN] = {};
		// InetNtopA: 네트워크 바이트 순서 IPv4 주소를 점 표기 문자열로 변환한다
		::InetNtopA(AF_INET, &SockAddr.sin_addr, Buffer, INET_ADDRSTRLEN);
		return std::string(Buffer);
	}
}