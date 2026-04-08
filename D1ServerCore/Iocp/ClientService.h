#pragma once

#include "Service.h"

namespace D1
{
	/**
	 * 클라이언트 측 Service.
	 * SessionFactory로 Session을 생성하고 서버에 연결한다.
	 */
	class ClientService : public Service
	{
	public:
		/**
		 * Session을 생성하고 ConnectEx로 서버에 연결한다.
		 *
		 * @param Address   연결할 서버 주소
		 * @return          생성된 Session (실패 시 nullptr)
		 */
		std::shared_ptr<Session> Connect(const SOCKADDR_IN& Address);
	};
}