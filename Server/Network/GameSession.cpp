#include "GameSession.h"
#include "Room.h"
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

// 접속 시 호출. 아직 로그인 전이므로 방 입장은 하지 않는다.
void GameSession::OnConnected()
{
	spdlog::info("Client connected");
}

// 메시지 수신 시 처리. 현재는 방에 있으면 브로드캐스트.
void GameSession::OnReceived(const String& Message)
{
	spdlog::info("Received: {}", Message);

	if (CurrentRoom)
	{
		auto Self = std::static_pointer_cast<GameSession>(shared_from_this());
		CurrentRoom->Push([Room = CurrentRoom, Self, Message]()
		{
			Room->Broadcast(Message, Self);
		});
	}
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