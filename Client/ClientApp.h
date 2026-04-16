#pragma once

#include "Types.h"
#include "Packet/SendBuffer.h"
#include "ClientSession.h"

// 클라이언트 애플리케이션 최상위 클래스.
// 서버 연결, 회원가입/로그인, 채팅 흐름을 관리한다.
class ClientApp
{
public:
	ClientApp();
	~ClientApp();

	void Run();

private:
	void AuthMenu();
	void ChatLoop();

	SendBufferManager SendBufferManagerInstance_;
	IoContext Context_;
	SharedPtr<ClientSession> SessionPtr_;
};