#pragma once

#include "Types.h"
#include "Lock.h"

class Room;

// 채팅방 목록을 관리한다. 방 생성, 조회, 삭제를 담당.
class RoomManager
{
public:
	SharedPtr<Room> CreateRoom(const WString& roomName);
	SharedPtr<Room> FindRoom(uint32 RoomId);
	Vector<SharedPtr<Room>> GetRoomList();
	void RemoveRoom(uint32 RoomId);

private:
	USE_LOCK;
	HashMap<uint32, SharedPtr<Room>> Rooms;
	Atomic<uint32> NextRoomId = 1;
};