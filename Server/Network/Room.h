#pragma once

#include "Types.h"
#include "JobQueue.h"

class GameSession;

// 채팅 방. JobQueue를 상속받아 내부 로직을 직렬화한다.
class Room : public JobQueue
{
public:
	Room(uint32 RoomId, const WString& Title);

	void Enter(SharedPtr<GameSession> SessionPtr);
	void Leave(SharedPtr<GameSession> SessionPtr);
	void Broadcast(const String& Message, SharedPtr<GameSession> Sender);

	uint32 GetRoomId() const { return RoomId; }
	const WString& GetTitle() const { return Title; }

private:
	uint32 RoomId = 0;
	WString Title;
	Set<SharedPtr<GameSession>> Sessions;
};