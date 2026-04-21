#pragma once

#include "Types.h"
#include "JobQueue.h"
#include "Packet/SendBuffer.h"

class GameSession;

// 채팅 방. JobQueue를 상속받아 내부 로직을 직렬화한다.
class Room : public JobQueue
{
public:
	Room(uint32 roomId, const WString& roomName);

	void Enter(SharedPtr<GameSession> SessionPtr);
	void Leave(SharedPtr<GameSession> SessionPtr);
	void Broadcast(SendBufferRef Buffer, SharedPtr<GameSession> Sender);

	uint32 GetRoomId() const { return RoomId; }
	const WString& GetRoomName() const { return RoomName; }

private:
	uint32 RoomId = 0;
	WString RoomName;
	HashMap<uint64, SharedPtr<GameSession>> Sessions;
	bool   bDead  = false;  // 마지막 유저가 나가 RoomManager에서 제거된 방. 이후 Enter 거부.
};