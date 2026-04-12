#include "Room.h"
#include "GameSession.h"
#include <spdlog/spdlog.h>

// Room 생성자. Strand를 io_context로부터 생성한다.
Room::Room(IoContext& Context)
	: Strand(asio::make_strand(Context))
{
}

// 세션을 방에 입장시킨다. Strand 안에서 호출되어야 한다.
void Room::Enter(SharedPtr<GameSession> SessionPtr)
{
	Sessions.insert(SessionPtr);
	spdlog::info("Session entered room. Total: {}", Sessions.size());
}

// 세션을 방에서 퇴장시킨다. Strand 안에서 호출되어야 한다.
void Room::Leave(SharedPtr<GameSession> SessionPtr)
{
	Sessions.erase(SessionPtr);
	spdlog::info("Session left room. Total: {}", Sessions.size());
}

// 방의 모든 세션에 메시지를 전달한다. Strand 안에서 호출되어야 한다.
void Room::Broadcast(const String& Message, SharedPtr<GameSession> Sender)
{
	for (auto& SessionPtr : Sessions)
	{
		SessionPtr->Send(Message);
	}
}

// Room의 Strand를 반환한다.
asio::strand<IoContext::executor_type>& Room::GetStrand()
{
	return Strand;
}