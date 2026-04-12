#pragma once

#include "Session.h"

class Room;

// 서버 측 세션. 기본 Session을 상속받아 Room 연동 로직을 추가한다.
class GameSession : public Session
{
public:
	GameSession(TcpSocket Socket, Room& RoomRef);

protected:
	void OnConnected() override;
	void OnReceived(const String& Message) override;
	void OnDisconnected() override;

private:
	Room& RoomRef;
};