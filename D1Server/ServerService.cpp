#include "ServerService.h"
#include <iostream>

namespace D1
{
	bool ServerService::Start(const NetAddress& Address)
	{
		if (Service::Start() == false)
			return false;
		if (ServerListener.Start(Address, weak_from_this()) == false)
			return false;
		std::cout << "[ServerService] Started (" << Address.GetIp() << ":" << Address.GetPort() << ")" << std::endl;
		return true;
	}

	void ServerService::Stop()
	{
		Service::Stop();
		std::cout << "[ServerService] Stopped" << std::endl;
	}
}