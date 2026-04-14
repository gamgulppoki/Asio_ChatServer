#pragma once

#include "Types.h"

// 클라이언트 접속 수락을 전담하는 클래스.
class Listener
{
public:
	Listener(IoContext& Context, uint16 iPort);

	void Start();

private:
	asio::awaitable<void> DoAccept();

	TcpAcceptor Acceptor_;
};