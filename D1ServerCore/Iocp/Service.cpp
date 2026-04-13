#include "Service.h"
#include "Session.h"
#include <iostream>

namespace D1
{
	Service::Service()
	{
	}

	Service::~Service()
	{
		Stop();
	}

	bool Service::Start()
	{
		return Core.Initialize();
	}

	void Service::Stop()
	{
		if (Sessions.empty())
			return;
		Sessions.clear();
		std::cout << "[Service] All sessions released" << std::endl;
	}

	void Service::SetSessionFactory(SessionFactory InFactory)
	{
		Factory = InFactory;
	}

	SessionRef Service::CreateSession()
	{
		if (Factory == nullptr)
		{
			std::cout << "[Service] SessionFactory not set!" << std::endl;
			return nullptr;
		}
		SessionRef NewSession = Factory();
		NewSession->SetOwnerService(weak_from_this());
		Sessions.insert(NewSession);
		return NewSession;
	}

	void Service::ReleaseSession(SessionRef Target)
	{
		Sessions.erase(Target);
	}
}