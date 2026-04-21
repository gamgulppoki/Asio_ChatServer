#include "GameSession.h"
#include "Room.h"
#include "ClientPacketHandler.h"
#include "Packet/PacketHeader.h"
#include <spdlog/spdlog.h>

GameSession::GameSession(TcpSocket Socket)
	: Session(std::move(Socket))
{
}

// Room을 설정한다. 로비에서 방 입장 시 호출.
void GameSession::SetRoom(SharedPtr<Room> RoomPtr)
{
	CurrentRoom = RoomPtr;
}

// 접속 시 호출. PlayerId는 로그인 성공 시점에 DB User.Id로 세팅된다.
void GameSession::OnConnected()
{
	spdlog::info("Client connected");
}

// 수신 데이터에서 완성된 패킷을 꺼내 처리한다. 처리한 바이트 수를 반환.
int32 GameSession::OnReceived(BYTE* Buffer, int32 iLen)
{
	int32 iProcessLen = 0;

	while (iLen >= sizeof(PacketHeader))
	{
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);

		if (iLen < Header->iSize)
			break;

		ClientPacketHandler::HandlePacket(
			std::static_pointer_cast<Session>(shared_from_this()), Buffer, Header->iSize);

		Buffer += Header->iSize;
		iLen -= Header->iSize;
		iProcessLen += Header->iSize;
	}

	return iProcessLen;
}

// 연결 종료 시 방에서 퇴장한다.
void GameSession::OnDisconnected()
{
	spdlog::info("Client disconnected");

	if (CurrentRoom)
	{
		auto Self = std::static_pointer_cast<GameSession>(shared_from_this());
		CurrentRoom->Push([Room = CurrentRoom, Self]()
		{
			Room->Leave(Self);
		});
		CurrentRoom = nullptr;
	}
}