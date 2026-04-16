#include "Service.h"
#include "Session.h"
#include <cassert>
#include <iostream>
#include <utility>

Service::Service(NetAddress InAddress, SessionFactory InFactory)
	: Factory(std::move(InFactory)), Address(InAddress)
{
	const bool bFactoryValid = static_cast<bool>(Factory);
	assert(bFactoryValid && "Service requires a valid SessionFactory");
	(void)bFactoryValid;
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

SessionRef Service::CreateSession()
{
	SessionRef NewSession = Factory();
	NewSession->SetOwnerService(weak_from_this());
	Sessions.insert(NewSession);
	return NewSession;
}

void Service::ReleaseSession(SessionRef Target)
{
	Sessions.erase(Target);
}
