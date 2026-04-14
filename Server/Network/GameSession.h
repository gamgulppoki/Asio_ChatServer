#pragma once

#include "Session.h"
#include "PlayerInfo.h"

class Room;

// 서버 측 세션. 기본 Session을 상속받아 Room 연동 로직을 추가한다.
class GameSession : public Session
{
public:
	GameSession(TcpSocket Socket);

	void SetRoom(SharedPtr<Room> RoomPtr);
	SharedPtr<Room> GetRoom() const { return CurrentRoom; }

	PlayerInfo& GetPlayerInfo() { return Info; }
	uint64 GetPlayerId() const { return Info.PlayerId; }

protected:
	void OnConnected() override;
	int32 OnReceived(BYTE* Buffer, int32 iLen) override;
	void OnDisconnected() override;

private:
	SharedPtr<Room> CurrentRoom;
	PlayerInfo Info;
};