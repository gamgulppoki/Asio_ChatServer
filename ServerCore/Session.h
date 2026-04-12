#pragma once

#include "Types.h"

// 기본 세션. 소켓의 비동기 read/write를 코루틴으로 처리한다.
// 서버/클라이언트에서 상속받아 OnConnected, OnReceived, OnDisconnected를 구현한다.
class Session : public std::enable_shared_from_this<Session>
{
public:
	Session(TcpSocket Socket);
	virtual ~Session() = default;

	void Start();
	void Send(const String& Message);
	void Disconnect();

protected:
	virtual void OnConnected() {}
	virtual void OnReceived(const String& Message) {}
	virtual void OnDisconnected() {}

private:
	asio::awaitable<void> DoRead();
	asio::awaitable<void> DoWrite();

	TcpSocket Socket;
	Deque<String> WriteQueue;
	bool bIsWriting = false;
};