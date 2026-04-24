#include "Room.h"
#include "GameSession.h"
#include "RoomManager.h"
#include "../ServerGlobal.h"
#include <spdlog/spdlog.h>

Room::Room(uint32 roomId, const WString& roomName)
	: RoomId(roomId)
	, RoomName(roomName)
{
}

// 세션을 방에 입장시킨다. JobQueue를 통해 직렬화되어 호출된다.
void Room::Enter(SharedPtr<GameSession> SessionPtr)
{
	uint64 PlayerId = SessionPtr->GetPlayerId();

	// 이미 제거된 방이면 입장 거부. (FindRoom -> Push -> Enter 사이에 마지막 유저가 나간 케이스)
	if (bDead)
	{
		spdlog::warn("Player {} tried to enter dead room {}", PlayerId, RoomId);
		return;
	}

	Sessions[PlayerId] = SessionPtr;
	spdlog::info("Player {} entered room {}. Total: {}", PlayerId, RoomId, Sessions.size());
}

// 세션을 방에서 퇴장시킨다. 마지막 세션이 나가면 휘발 방 규칙에 따라 RoomManager에서 방을 제거한다.
void Room::Leave(SharedPtr<GameSession> SessionPtr)
{
	uint64 PlayerId = SessionPtr->GetPlayerId();
	Sessions.erase(PlayerId);
	spdlog::info("Player {} left room {}. Total: {}", PlayerId, RoomId, Sessions.size());

	if (Sessions.empty())
	{
		bDead = true;
		GRoomManager->RemoveRoom(RoomId);
		spdlog::info("Room {} removed (empty)", RoomId);
	}
}

// 방의 모든 세션에 패킷을 전달한다.
void Room::Broadcast(SendBufferRef Buffer)
{
	for (auto& [PlayerId, weakSessionPtr] : Sessions)
	{
		if (auto SessionPtr = weakSessionPtr.lock())
			SessionPtr->Send(Buffer);
	}
}