#pragma once

#include "Types.h"
#include "GlobalQueue.h"
#include "Packet/SendBuffer.h"
#include "Network/RoomManager.h"

// 서버 애플리케이션 최상위 클래스.
// 전역 싱글톤 생명주기를 관리하고, 서버 가동/종료 흐름을 캡슐화한다.
class ServerApp
{
public:
	ServerApp();
	~ServerApp();

	void Run();

private:
	void InitRooms();

	GlobalQueue       GlobalQueueInstance_;
	SendBufferManager SendBufferManagerInstance_;
	RoomManager       RoomManagerInstance_;
};