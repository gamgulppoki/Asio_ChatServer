#pragma once

#include "Types.h"
#include "PlayerInfo.h"
#include "Packet/RecvBuffer.h"
#include "Packet/SendBuffer.h"
#include <mutex>

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

	// 소켓의 executor (io_context). 이 세션에 묶인 비동기 작업(예: AI 응답 스트리밍)을 띄울 때 쓴다.
	asio::any_io_executor GetExecutor() { return Socket.get_executor(); }

protected:
	virtual void OnConnected() {}
	virtual int32 OnReceived(BYTE* Buffer, int32 iLen) { return iLen; }
	virtual void OnDisconnected() {}

private:
	asio::awaitable<void> DoRead();
	asio::awaitable<void> DoWrite();

	TcpSocket Socket;
	RecvBuffer RecvBuf;

	// 송신 큐 + "쓰는 중" 플래그. 반드시 WriteMutex 안에서만 만진다.
	// io_context 를 워커 N 개가 poll 하므로 Send()(아무 스레드)와 DoWrite 코루틴(아무 스레드)이
	// 동시에 큐를 만질 수 있다. 부하 테스트에서 실제로 큐가 깨져 WSAEFAULT·유실·크래시가 났고,
	// 이 프로젝트가 JobQueue / SendBufferManager 에 쓰는 것과 같은 방식(짧은 임계영역의 std::mutex)으로 막는다.
	std::mutex WriteMutex;
	Queue<SendBufferRef> WriteQueue;
	bool bIsWriting = false;

protected:
	PlayerInfo Info;
	SessionState State = SessionState::Connected;
};