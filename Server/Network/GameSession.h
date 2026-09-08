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

	// AI 응답이 진행 중인지. 한 세션에 하나만 (스트림이 섞이지 않게).
	std::atomic<bool> AiBusy{ false };

protected:
	void OnConnected() override;
	int32 OnReceived(BYTE* Buffer, int32 iLen) override;
	void OnDisconnected() override;

private:
	SharedPtr<Room> CurrentRoom;
	PlayerInfo Info;
};