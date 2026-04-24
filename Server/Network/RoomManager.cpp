#include "RoomManager.h"
#include "Room.h"

// 새 방을 생성하고 목록에 추가한다.
SharedPtr<Room> RoomManager::CreateRoom(const WString& roomName)
{
	uint32 RoomId = NextRoomId.fetch_add(1);
	auto NewRoom = std::make_shared<Room>(RoomId, roomName);

	{
		std::unique_lock lock(Lock);
		Rooms[RoomId] = NewRoom;
	}

	return NewRoom;
}

// ID로 방을 찾는다. 없으면 nullptr.
SharedPtr<Room> RoomManager::FindRoom(uint32 RoomId)
{
	std::shared_lock lock(Lock);
	auto It = Rooms.find(RoomId);
	if (It == Rooms.end())
		return nullptr;

	return It->second;
}

// 현재 존재하는 방 목록을 반환한다.
Vector<SharedPtr<Room>> RoomManager::GetRoomList()
{
	std::shared_lock lock(Lock);
	Vector<SharedPtr<Room>> Result;
	Result.reserve(Rooms.size());

	for (auto& [Id, RoomPtr] : Rooms)
		Result.push_back(RoomPtr);

	return Result;
}

// 방을 목록에서 제거한다.
void RoomManager::RemoveRoom(uint32 RoomId)
{
	std::unique_lock lock(Lock);
	Rooms.erase(RoomId);
}

void RoomManager::Broadcast(SendBufferRef buffer)
{
	auto RoomList = GetRoomList();
	
	for (auto& RoomPtr : RoomList)
	{
		RoomPtr->Push([buffer, RoomPtr] 
		{
			RoomPtr->Broadcast(buffer);
		});
	}
}
