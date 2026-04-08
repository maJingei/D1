#pragma once

#include "Service.h"
#include "Listener.h"

namespace D1
{
	/**
	 * 서버 측 Service.
	 * Listener를 소유하여 클라이언트 접속을 수용하고,
	 * SessionFactory로 생성한 Session을 관리한다.
	 */
	class ServerService : public Service
	{
	public:
		/**
		 * IocpCore 초기화 + Listener 시작.
		 *
		 * @param Address   바인드할 주소 (IP + Port)
		 * @return          시작 성공 여부
		 */
		bool Start(const SOCKADDR_IN& Address);

		/** Listener 중지 + 모든 세션 정리. */
		void Stop() override;

	private:
		Listener ServerListener;
	};
}
