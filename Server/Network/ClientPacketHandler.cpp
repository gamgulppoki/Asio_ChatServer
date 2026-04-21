#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "Room.h"
#include "RoomManager.h"
#include "../ServerGlobal.h"
#include "../DB/DBConnectionPool.h"
#include "../Security/InputValidator.h"
#include "StringUtils.h"
#include <spdlog/spdlog.h>

#include "DB/Entities/Entities.h"
#include "DB/ORM/DBContext.h"
#include "DB/Generated/EntitiesGenerated.h"

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
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());
	std::unique_ptr<User> newUserPtr = std::make_unique<User>();
	
	// 이메일 중복 확인
	auto Existing = dbContext.Set<User>().Where(Col<User>::Email == Pkt.email()).ToList();
	if (!Existing.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email already registered");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// User 구조체 채우기
	newUserPtr->Nickname = Pkt.name();
	newUserPtr->Email = Pkt.email();
	newUserPtr->Password = Pkt.password();
	
	dbContext.Set<User>().Add(std::move(newUserPtr));
	
	if (!dbContext.SaveChanges())
	{
		// 실패 분기
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to insert user: {}", Pkt.email());
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 성공 분기
	ResPkt.set_success(true);
	ResPkt.set_msg("Registration successful");
	spdlog::info("User registered: {}", Pkt.email());
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}

// 로그인 요청을 처리한다.
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

	// DB에서 유저 조회
	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());
	
	auto Existing = dbContext.Set<User>().Where(Col<User>::Email == Pkt.email()).ToList();

	// 이메일 못 찾음
	if (Existing.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	User* FoundUser = Existing.front();

	// 비밀번호 검증 (평문 비교)
	if (FoundUser->Password.value() != Pkt.password())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Wrong password");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 로그인 성공 -- 세션에 닉네임 + User.Id 저장
	const std::string& Nickname = FoundUser->Nickname.value();
	GameSessionPtr->GetPlayerInfo().Nickname = StringUtils::Utf8ToWide(Nickname);
	GameSessionPtr->GetPlayerInfo().PlayerId = FoundUser->Id.value();
	ResPkt.set_success(true);
	ResPkt.set_msg("Login successful");
	ResPkt.set_name(Nickname);
	spdlog::info("User logged in: {} ({})", Nickname, Pkt.email());
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

// 방 생성 요청을 처리한다.
bool Handle_C_CREATE_ROOM(SharedPtr<Session> SessionPtr, Protocol::C_CREATE_ROOM& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	auto newRoom = GRoomManager->CreateRoom(StringUtils::Utf8ToWide(Pkt.roomname()));

	Protocol::S_CREATE_ROOM ResPkt;
	ResPkt.set_success(newRoom != nullptr);
	if (newRoom)
		ResPkt.set_roomid(newRoom->GetRoomId());
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));

	if (newRoom)
		spdlog::info("Room created: id={}, name={}", newRoom->GetRoomId(), Pkt.roomname());

	return true;
}

// 방 리스트 요청을 처리한다.
bool Handle_C_GET_ROOM_LIST(SharedPtr<Session> SessionPtr, Protocol::C_GET_ROOM_LIST& Pkt)
{
	auto list = GRoomManager->GetRoomList();
	
	Protocol::S_GET_ROOM_LIST ResPkt;
	ResPkt.set_success(true);
	
	for (const auto& room :	list)
	{
		Protocol::Room* RoomMsg = ResPkt.add_rooms();
		RoomMsg->set_roomid((uint32)room->GetRoomId());
		RoomMsg->set_roomname(StringUtils::WideToUtf8(room->GetRoomName()));
	}
	
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	
	return true;
}

// 방 퇴장 요청을 처리한다.
bool Handle_C_EXIT_ROOM(SharedPtr<Session> SessionPtr, Protocol::C_EXIT_ROOM& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	auto RoomPtr = GameSessionPtr->GetRoom();

	Protocol::S_EXIT_ROOM ResPkt;

	if (RoomPtr)
	{
		uint32 RoomId = RoomPtr->GetRoomId();
		RoomPtr->Push([RoomPtr, GameSessionPtr]()
		{
			RoomPtr->Leave(GameSessionPtr);
		});
		GameSessionPtr->SetRoom(nullptr);
		ResPkt.set_success(1);
		spdlog::info("Player {} left room {}", GameSessionPtr->GetPlayerId(), RoomId);
	}
	else
	{
		ResPkt.set_success(0);
	}

	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
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
	ChatPkt.set_msg(Pkt.msg());
	ChatPkt.set_name(StringUtils::WideToUtf8(GameSessionPtr->GetPlayerInfo().Nickname));

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

// 닉네임 변경 요청을 처리한다.
bool Handle_C_UPDATE_NICKNAME(SharedPtr<Session> SessionPtr, Protocol::C_UPDATE_NICKNAME& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_UPDATE_NICKNAME ResPkt;

	// 입력 검증
	if (!InputValidator::IsValidName(Pkt.newnickname()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid nickname (2-20 characters)");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const uint64 UserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (UserId == 0)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not logged in");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// DB 조회 및 업데이트
	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	auto Users = dbContext.Set<User>().Where(Col<User>::Id == static_cast<int64>(UserId)).ToList();
	if (Users.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("User not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	User* FoundUser = Users.front();
	FoundUser->Nickname = Pkt.newnickname();

	if (!dbContext.SaveChanges())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to update nickname for UserId {}", UserId);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 세션 PlayerInfo 동기화
	GameSessionPtr->GetPlayerInfo().Nickname = StringUtils::Utf8ToWide(Pkt.newnickname());

	ResPkt.set_success(true);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	spdlog::info("UserId {} changed nickname to {}", UserId, Pkt.newnickname());
	return true;
}

// 계정 탈퇴 요청을 처리한다.
bool Handle_C_DELETE_ACCOUNT(SharedPtr<Session> SessionPtr, Protocol::C_DELETE_ACCOUNT& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_DELETE_ACCOUNT ResPkt;

	const uint64 UserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (UserId == 0)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not logged in");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// DB 조회 및 삭제
	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	auto Users = dbContext.Set<User>().Where(Col<User>::Id == static_cast<int64>(UserId)).ToList();
	if (Users.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("User not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	dbContext.Set<User>().Delete(Users.front());

	if (!dbContext.SaveChanges())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to delete UserId {}", UserId);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// 방에 있다면 Leave 처리
	auto RoomPtr = GameSessionPtr->GetRoom();
	if (RoomPtr)
	{
		RoomPtr->Push([RoomPtr, GameSessionPtr]()
		{
			RoomPtr->Leave(GameSessionPtr);
		});
		GameSessionPtr->SetRoom(nullptr);
	}

	ResPkt.set_success(true);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	spdlog::info("UserId {} deleted account", UserId);

	// 클라가 응답 받고 자체적으로 Disconnect할 예정이므로 서버에서 먼저 끊지 않음
	return true;
}
