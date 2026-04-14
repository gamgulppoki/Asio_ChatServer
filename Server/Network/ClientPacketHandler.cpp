#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "Room.h"
#include "RoomManager.h"
#include "../ServerGlobal.h"
#include <spdlog/spdlog.h>

// 등록되지 않은 패킷 ID가 들어왔을 때 호출된다.
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
{
	PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
	spdlog::warn("Unknown packet id: {}", Header->iId);
	return false;
}

// 클라이언트가 방 입장을 요청하면, 해당 방에 입장시킨다.
bool Handle_C_ENTER_ROOM(SharedPtr<Session> SessionPtr, Protocol::C_ENTER_ROOM& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);

	SharedPtr<Room> RoomPtr = GRoomManager->FindRoom(Pkt.roomid());

	Protocol::S_ENTER_ROOM ResPkt;

	if (RoomPtr == nullptr)
	{
		ResPkt.set_success(false);
		ResPkt.set_roomid(Pkt.roomid());
		spdlog::warn("Player {} tried to enter non-existent room {}", GameSessionPtr->GetPlayerId(), Pkt.roomid());
	}
	else
	{
		// 기존 방에서 나가기
		auto OldRoom = GameSessionPtr->GetRoom();
		if (OldRoom)
		{
			OldRoom->Push([OldRoom, GameSessionPtr]()
			{
				OldRoom->Leave(GameSessionPtr);
			});
		}

		GameSessionPtr->SetRoom(RoomPtr);
		RoomPtr->Push([RoomPtr, GameSessionPtr]()
		{
			RoomPtr->Enter(GameSessionPtr);
		});

		ResPkt.set_success(true);
		ResPkt.set_roomid(Pkt.roomid());
		spdlog::info("Player {} entered room {}", GameSessionPtr->GetPlayerId(), Pkt.roomid());
	}

	SendBufferRef Buffer = ClientPacketHandler::MakeSendBuffer(ResPkt);
	GameSessionPtr->Send(Buffer);

	return true;
}

// 클라이언트가 보낸 채팅 메시지를 Room 전체에 브로드캐스트한다.
bool Handle_C_CHAT(SharedPtr<Session> SessionPtr, Protocol::C_CHAT& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);

	Protocol::S_CHAT ChatPkt;
	ChatPkt.set_playerid(GameSessionPtr->GetPlayerId());
	ChatPkt.set_msg(Pkt.msg());

	SendBufferRef Buffer = ClientPacketHandler::MakeSendBuffer(ChatPkt);

	auto RoomPtr = GameSessionPtr->GetRoom();
	if (RoomPtr)
	{
		RoomPtr->Push([RoomPtr, Buffer, GameSessionPtr]()
		{
			RoomPtr->Broadcast(Buffer, GameSessionPtr);
		});
	}

	return true;
}
