#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "Room.h"
#include "RoomManager.h"
#include "../ServerGlobal.h"
#include "../DB/DBConnectionPool.h"
#include "../DB/Models/UserModel.h"
#include "../DB/Models/UserCols.h"
#include "../Security/AuthUtils.h"
#include "../Security/InputValidator.h"
#include "StringUtils.h"
#include <spdlog/spdlog.h>

// 회원가입 요청을 처리한���.
bool Handle_C_REGISTER(SharedPtr<Session> SessionPtr, Protocol::C_REGISTER& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_REGISTER ResPkt;

	// 입력 검증
	if (!InputValidator::IsValidName(Pkt.name()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid name (2-20 characters)");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}
	if (!InputValidator::IsValidEmail(Pkt.email()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid email format");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}
	if (!InputValidator::IsValidPassword(Pkt.password()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid password (8-64 characters)");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// DB 작업
	DBConnectionScope Scope(GDBPool);
	auto Model = CreateUserModel(*Scope.Get());

	// 이메일 중복 확인
	WString WideEmail = StringUtils::Utf8ToWide(Pkt.email());
	auto Existing = Model.SelectOne(UserCols::Email == WideEmail.c_str());
	if (Existing.has_value())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email already registered");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 비밀번호 해싱
	std::string EncodedHash;
	if (!AuthUtils::HashPassword(Pkt.password(), EncodedHash))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Internal error");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// User 구조체 채우기
	User NewUser = {};
	WString WideName = StringUtils::Utf8ToWide(Pkt.name());
	WString WideHash = StringUtils::Utf8ToWide(EncodedHash);
	::wcscpy_s(NewUser.Name, WideName.c_str());
	::wcscpy_s(NewUser.Email, WideEmail.c_str());
	::wcscpy_s(NewUser.PasswordHash, WideHash.c_str());

	if (!Model.Insert(NewUser))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to insert user: {}", Pkt.email());
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	ResPkt.set_success(true);
	ResPkt.set_msg("Registration successful");
	spdlog::info("User registered: {}", Pkt.email());
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}

// ���그인 요청을 처리한다.
bool Handle_C_LOGIN(SharedPtr<Session> SessionPtr, Protocol::C_LOGIN& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_LOGIN ResPkt;

	// 입력 검증
	if (!InputValidator::IsValidEmail(Pkt.email()) || !InputValidator::IsValidPassword(Pkt.password()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid input");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// DB에서 유�� 조회
	DBConnectionScope Scope(GDBPool);
	auto Model = CreateUserModel(*Scope.Get());

	WString WideEmail = StringUtils::Utf8ToWide(Pkt.email());
	auto Found = Model.SelectOne(UserCols::Email == WideEmail.c_str());
	if (!Found.has_value())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 비밀번호 검증
	std::string StoredHash = StringUtils::WideToUtf8(Found->PasswordHash);
	if (!AuthUtils::VerifyPassword(StoredHash, Pkt.password()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Wrong password");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 로그인 성공
	std::string Utf8Name = StringUtils::WideToUtf8(Found->Name);
	ResPkt.set_success(true);
	ResPkt.set_msg("Login successful");
	ResPkt.set_name(Utf8Name);
	spdlog::info("User logged in: {}", Pkt.email());
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}

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
