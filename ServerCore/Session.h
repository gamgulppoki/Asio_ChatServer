#pragma once

#include "Types.h"
#include "PlayerInfo.h"
#include "Packet/RecvBuffer.h"
#include "Packet/SendBuffer.h"

// 기본 세션. 소켓의 비동기 read/write를 코루틴으로 처리한다.
// 서버/클라이언트에서 상속받아 OnConnected, OnReceived, OnDisconnected를 구현한다.
class Session : public std::enable_shared_from_this<Session>
{
public:
	Session(TcpSocket Socket);
	virtual ~Session() = default;

	void Start();
	void Send(SendBufferRef Buffer);
	void Disconnect();

protected:
	virtual void OnConnected() {}
	virtual int32 OnReceived(BYTE* Buffer, int32 iLen) { return iLen; }
	virtual void OnDisconnected() {}

private:
	asio::awaitable<void> DoRead();
	asio::awaitable<void> DoWrite();

	TcpSocket Socket;

	// 세션의 모든 핸들러(DoRead/DoWrite 코루틴, Send 의 post)가 이 strand 위에서만 돈다.
	// io_context 를 워커 N 개가 poll 하므로, strand 가 없으면 Send() 의 post 와 DoWrite 가
	// 서로 다른 스레드에서 WriteQueue 를 동시에 만진다 (부하 테스트에서 실제로 크래시·유실이 났다).
	Strand SessionStrand;

	RecvBuffer RecvBuf;
	Queue<SendBufferRef> WriteQueue;
	bool bIsWriting = false;

protected:
	PlayerInfo Info;
	SessionState State = SessionState::Connected;
};