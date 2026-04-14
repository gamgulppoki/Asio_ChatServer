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
	uint64 PlayerId = SessionPtr->GetPlayerId();
	Sessions[PlayerId] = SessionPtr;
	spdlog::info("Player {} entered room {}. Total: {}", PlayerId, RoomId, Sessions.size());
}

// 세션을 방에서 퇴장시킨다.
void Room::Leave(SharedPtr<GameSession> SessionPtr)
{
	uint64 PlayerId = SessionPtr->GetPlayerId();
	Sessions.erase(PlayerId);
	spdlog::info("Player {} left room {}. Total: {}", PlayerId, RoomId, Sessions.size());
}

// 방의 모든 세션에 패킷을 전달한다.
void Room::Broadcast(SendBufferRef Buffer, SharedPtr<GameSession> Sender)
{
	for (auto& [PlayerId, SessionPtr] : Sessions)
	{
		SessionPtr->Send(Buffer);
	}
}