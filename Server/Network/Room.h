#pragma once

#include "Types.h"

class GameSession;

// 채팅 방. Strand로 동시성을 보호하며, 소속 세션에 메시지를 브로드캐스트한다.
class Room
{
public:
	explicit Room(IoContext& Context);

	void Enter(SharedPtr<GameSession> SessionPtr);
	void Leave(SharedPtr<GameSession> SessionPtr);
	void Broadcast(const String& Message, SharedPtr<GameSession> Sender);

	asio::strand<IoContext::executor_type>& GetStrand();

private:
	asio::strand<IoContext::executor_type> Strand;
	Set<SharedPtr<GameSession>> Sessions;
};