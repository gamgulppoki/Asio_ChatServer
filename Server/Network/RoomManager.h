#pragma once

#include "Types.h"
#include "Lock.h"
#include <shared_mutex>

#include "Packet/SendBuffer.h"

class Job;
class Room;

// 채팅방 목록을 관리한다. 방 생성, 조회, 삭제를 담당.
class RoomManager
{
public:
	SharedPtr<Room> CreateRoom(const WString& roomName);
	SharedPtr<Room> FindRoom(uint32 RoomId);
	Vector<SharedPtr<Room>> GetRoomList();
	void RemoveRoom(uint32 RoomId);
	
	// 확성기용 메시지 모든 방 전파
	void Broadcast(SendBufferRef buffer);

	// 모든 Room 을 해제한다. 서버 종료 시 호출.
	void Clear();

private:
	std::shared_mutex Lock;
	HashMap<uint32, SharedPtr<Room>> Rooms;
	Atomic<uint32> NextRoomId = 1;
};