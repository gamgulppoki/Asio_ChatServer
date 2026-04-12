#include "GameSession.h"
#include "Room.h"
#include <spdlog/spdlog.h>

// GameSession 생성자. 기본 Session에 Room 참조를 추가한다.
GameSession::GameSession(TcpSocket Socket, Room& RoomRef)
	: Session(std::move(Socket))
	, RoomRef(RoomRef)
{
}

// 접속 시 방에 입장한다.
void GameSession::OnConnected()
{
	spdlog::info("Client connected");

	auto Self = std::static_pointer_cast<GameSession>(shared_from_this());
	asio::post(RoomRef.GetStrand(), [this, Self]()
	{
		RoomRef.Enter(Self);
	});
}

// 메시지 수신 시 방에 브로드캐스트한다.
void GameSession::OnReceived(const String& Message)
{
	spdlog::info("Received: {}", Message);

	auto Self = std::static_pointer_cast<GameSession>(shared_from_this());
	asio::post(RoomRef.GetStrand(), [this, Self, Message]()
	{
		RoomRef.Broadcast(Message, Self);
	});
}

// 연결 종료 시 방에서 퇴장한다.
void GameSession::OnDisconnected()
{
	spdlog::info("Client disconnected");

	auto Self = std::static_pointer_cast<GameSession>(shared_from_this());
	asio::post(RoomRef.GetStrand(), [this, Self]()
	{
		RoomRef.Leave(Self);
	});
}