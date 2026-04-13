#include "Room.h"
#include "GameSession.h"
#include <spdlog/spdlog.h>

Room::Room(uint32 RoomId, const WString& Title)
	: RoomId(RoomId)
	, Title(Title)
{
}

// 세션을 방에 입장시킨다. JobQueue를 통해 직렬화되어 호출된다.
void Room::Enter(SharedPtr<GameSession> SessionPtr)
{
	Sessions.insert(SessionPtr);
	spdlog::info("Session entered room {}. Total: {}", RoomId, Sessions.size());
}

// 세션을 방에서 퇴장시킨다.
void Room::Leave(SharedPtr<GameSession> SessionPtr)
{
	Sessions.erase(SessionPtr);
	spdlog::info("Session left room {}. Total: {}", RoomId, Sessions.size());
}

// 방의 모든 세션에 메시지를 전달한다.
void Room::Broadcast(const String& Message, SharedPtr<GameSession> Sender)
{
	for (auto& SessionPtr : Sessions)
	{
		SessionPtr->Send(Message);
	}
}