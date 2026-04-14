#include "ServerPacketHandler.h"
#include "Session.h"
#include <spdlog/spdlog.h>
#include <iostream>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

// 등록되지 않은 패킷 ID가 들어왔을 때 호출된다.
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
{
	PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
	spdlog::warn("Unknown packet id: {}", Header->iId);
	return false;
}

// 방 입장 결과를 수신한다.
bool Handle_S_ENTER_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_ENTER_ROOM& Pkt)
{
	if (Pkt.success())
		std::cout << "[Room " << Pkt.roomid() << "] Entered successfully." << std::endl;
	else
		std::cout << "[Error] Failed to enter room " << Pkt.roomid() << "." << std::endl;

	return true;
}

// 서버로부터 채팅 메시지를 수신하여 출력한다.
bool Handle_S_CHAT(SharedPtr<Session> SessionPtr, Protocol::S_CHAT& Pkt)
{
	std::cout << "[Player " << Pkt.playerid() << "] " << Pkt.msg() << std::endl;
	return true;
}
