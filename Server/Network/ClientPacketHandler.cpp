#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "Room.h"
#include "RoomManager.h"
#include "../ServerGlobal.h"
#include "../DB/DBConnectionPool.h"
#include "../Security/InputValidator.h"
#include "StringUtils.h"
#include <spdlog/spdlog.h>

#include "SessionManager.h"
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

	// 닉네임 중복 확인 (DB UNIQUE 제약 대신 핸들러 단에서 처리)
	auto ExistingNickname = dbContext.Set<User>().Where(Col<User>::Nickname == Pkt.name()).ToList();
	if (!ExistingNickname.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Nickname already taken");
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
	GSessionManager->Register(FoundUser->Id.value(), GameSessionPtr);
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
		RoomPtr->Push([RoomPtr, Buffer]()
		{
			RoomPtr->Broadcast(Buffer);
		});
	}

	return true;
}

// 확성기 요청을 처리한다. 모든 방에 S_SHOUT 브로드캐스트.
bool Handle_C_SHOUT(SharedPtr<Session> SessionPtr, Protocol::C_SHOUT& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);

	Protocol::S_SHOUT ShoutPkt;
	ShoutPkt.set_msg(Pkt.msg());
	ShoutPkt.set_name(StringUtils::WideToUtf8(GameSessionPtr->GetPlayerInfo().Nickname));

	SendBufferRef Buffer = ClientPacketHandler::MakeSendBuffer(ShoutPkt);

	auto RoomManager = GRoomManager;
	if (RoomManager)
	{
		RoomManager->Broadcast(Buffer);
	}
	
	return true;
}

bool Handle_C_WHISPER(SharedPtr<Session> SessionPtr, Protocol::C_WHISPER& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);

	// 대상 조회 (닉네임 → PlayerId → Session)
	uint64 targetId = GSessionManager->GetPlayerId(Pkt.target_nickname());
	auto   targetSession = GSessionManager->GetSession(targetId);

	// 상대가 없거나 오프라인
	if (!targetSession)
	{
		Protocol::S_WHISPER ErrPkt;
		ErrPkt.set_success(false);
		ErrPkt.set_error_msg("상대를 찾을 수 없거나 오프라인입니다.");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ErrPkt));
		return true;
	}

	// 자기 자신에게는 차단
	if (targetId == GameSessionPtr->GetPlayerInfo().PlayerId)
	{
		Protocol::S_WHISPER ErrPkt;
		ErrPkt.set_success(false);
		ErrPkt.set_error_msg("자기 자신에게는 보낼 수 없습니다.");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ErrPkt));
		return true;
	}

	// 수신자에게만 송신. 발신자는 클라에서 로컬 에코하므로 서버 에코 불필요.
	Protocol::S_WHISPER OkPkt;
	OkPkt.set_success(true);
	OkPkt.set_from_name(StringUtils::WideToUtf8(GameSessionPtr->GetPlayerInfo().Nickname));
	OkPkt.set_message(Pkt.message());
	targetSession->Send(ClientPacketHandler::MakeSendBuffer(OkPkt));

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

// 친구 요청을 처리한다.
bool Handle_C_REQUEST_FRIEND(SharedPtr<Session> SessionPtr, Protocol::C_REQUEST_FRIEND& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_REQUEST_FRIEND ResPkt;

	// 입력 검증
	if (!InputValidator::IsValidEmail(Pkt.email()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid email format");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 MyUserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (MyUserId == 0)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not logged in");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	// 상대 User 조회
	auto targets = dbContext.Set<User>().Where(Col<User>::Email == Pkt.email()).ToList();
	if (targets.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 TargetUserId = targets.front()->Id.value();

	// 자기 자신 체크
	if (MyUserId == TargetUserId)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Cannot add yourself");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// (Me -> Target) 기존 관계 체크 (Pending/Accepted 둘 다)
	auto sameDir = dbContext.Set<Friendship>()
		.Where(Col<Friendship>::FromUserId == MyUserId)
		.Where(Col<Friendship>::ToUserId   == TargetUserId)
		.ToList();
	if (!sameDir.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Already requested or already friends");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// (Target -> Me) 역방향 pending 체크
	auto reverse = dbContext.Set<Friendship>()
		.Where(Col<Friendship>::FromUserId == TargetUserId)
		.Where(Col<Friendship>::ToUserId   == MyUserId)
		.ToList();
	if (!reverse.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("The other user already sent you a request. Accept from pending list.");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// INSERT
	auto newF = std::make_unique<Friendship>();
	newF->FromUserId = MyUserId;
	newF->ToUserId   = TargetUserId;
	newF->Status     = FriendStatus::Pending;
	dbContext.Set<Friendship>().Add(std::move(newF));

	if (!dbContext.SaveChanges())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to insert friendship: {} -> {}", MyUserId, TargetUserId);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	ResPkt.set_success(true);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	spdlog::info("Friend request: {} -> {}", MyUserId, TargetUserId);
	return true;
}

// 친구 요청 수락을 처리한다.
bool Handle_C_ACCEPT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::C_ACCEPT_FRIEND& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_ACCEPT_FRIEND ResPkt;

	if (!InputValidator::IsValidEmail(Pkt.email()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid email format");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 MyUserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (MyUserId == 0)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not logged in");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	// 요청자(상대) User 조회
	auto targets = dbContext.Set<User>().Where(Col<User>::Email == Pkt.email()).ToList();
	if (targets.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 OtherUserId = targets.front()->Id.value();

	// (Other -> Me) pending row 조회
	auto rows = dbContext.Set<Friendship>()
		.Where(Col<Friendship>::FromUserId == OtherUserId)
		.Where(Col<Friendship>::ToUserId   == MyUserId)
		.Where(Col<Friendship>::Status     == std::string(FriendStatus::Pending))
		.ToList();
	if (rows.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("No pending request from this user");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// Status Pending -> Accepted (dirty 자동 마킹)
	rows.front()->Status = FriendStatus::Accepted;
	if (!dbContext.SaveChanges())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to accept friendship: {} -> {}", OtherUserId, MyUserId);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	ResPkt.set_success(true);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	spdlog::info("Friend accepted: {} -> {}", OtherUserId, MyUserId);
	return true;
}

// 친구 요청 거절을 처리한다.
bool Handle_C_REJECT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::C_REJECT_FRIEND& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_REJECT_FRIEND ResPkt;

	if (!InputValidator::IsValidEmail(Pkt.email()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid email format");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 MyUserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (MyUserId == 0)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not logged in");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	// 요청자(상대) User 조회
	auto targets = dbContext.Set<User>().Where(Col<User>::Email == Pkt.email()).ToList();
	if (targets.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 OtherUserId = targets.front()->Id.value();

	// (Other -> Me) pending row 조회
	auto rows = dbContext.Set<Friendship>()
		.Where(Col<Friendship>::FromUserId == OtherUserId)
		.Where(Col<Friendship>::ToUserId   == MyUserId)
		.Where(Col<Friendship>::Status     == std::string(FriendStatus::Pending))
		.ToList();
	if (rows.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("No pending request from this user");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// DELETE
	dbContext.Set<Friendship>().Delete(rows.front());
	if (!dbContext.SaveChanges())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to reject friendship: {} -> {}", OtherUserId, MyUserId);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	ResPkt.set_success(true);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	spdlog::info("Friend rejected: {} -> {}", OtherUserId, MyUserId);
	return true;
}

// 받은 친구 요청 목록을 반환한다.
bool Handle_C_GET_PENDING_FRIENDS(SharedPtr<Session> SessionPtr, Protocol::C_GET_PENDING_FRIENDS& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_GET_PENDING_FRIENDS ResPkt;

	const int64 MyUserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (MyUserId == 0)
	{
		ResPkt.set_success(false);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	// 받은 pending 요청 조회 + 요청자 User 정보 eager load
	auto pendings = dbContext.Set<Friendship>()
		.Include(&Friendship::FromUser)
		.Where(Col<Friendship>::ToUserId == MyUserId)
		.Where(Col<Friendship>::Status   == std::string(FriendStatus::Pending))
		.ToList();

	ResPkt.set_success(true);
	for (Friendship* f : pendings)
	{
		User* fromUser = f->FromUser.Get();
		if (!fromUser) continue;

		Protocol::FriendInfo* info = ResPkt.add_pendings();
		info->set_email(fromUser->Email.value());
		info->set_nickname(fromUser->Nickname.value());
	}

	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}

// 친구 목록을 반환한다.
bool Handle_C_GET_FRIEND_LIST(SharedPtr<Session> SessionPtr, Protocol::C_GET_FRIEND_LIST& Pkt)
{	
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_GET_FRIEND_LIST ResPkt;

	const int64 MyUserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (MyUserId == 0)
	{
		ResPkt.set_success(false);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	auto friendList1 = dbContext.Set<Friendship>()
		.Include(&Friendship::ToUser)
		.Where(Col<Friendship>::FromUserId == MyUserId)
		.Where(Col<Friendship>::Status   == std::string(FriendStatus::Accepted))
		.ToList();
	
	auto friendList2 = dbContext.Set<Friendship>()
		.Include(&Friendship::FromUser)
		.Where(Col<Friendship>::ToUserId == MyUserId)
		.Where(Col<Friendship>::Status   == std::string(FriendStatus::Accepted))
		.ToList();

	ResPkt.set_success(true);
	for (Friendship* f : friendList1)
	{
		User* user = f->ToUser.Get();
		if (!user) continue;

		Protocol::FriendInfo* info = ResPkt.add_friends();
		info->set_email(user->Email.value());
		info->set_nickname(user->Nickname.value());
		info->set_is_online(GSessionManager->IsOnline(user->Id.value()));
	}

	for (Friendship* f : friendList2)
	{
		User* user = f->FromUser.Get();
		if (!user) continue;

		Protocol::FriendInfo* info = ResPkt.add_friends();
		info->set_email(user->Email.value());
		info->set_nickname(user->Nickname.value());
		info->set_is_online(GSessionManager->IsOnline(user->Id.value()));
	}

	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}

// 친구 삭제를 처리한다.
bool Handle_C_REMOVE_FRIEND(SharedPtr<Session> SessionPtr, Protocol::C_REMOVE_FRIEND& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_REMOVE_FRIEND ResPkt;

	if (!InputValidator::IsValidEmail(Pkt.email()))
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Invalid email format");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	const int64 MyUserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (MyUserId == 0)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not logged in");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	auto UserList = dbContext.Set<User>()
		.Where(Col<User>::Email == Pkt.email())
		.ToList();

	if (UserList.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Email not found");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}
	
	auto TargetId = UserList.front()->Id.value();
	
	// id로 accept인 관계를 delete해야함
	auto friendList1 = dbContext.Set<Friendship>()
		.Where(Col<Friendship>::ToUserId == MyUserId)
		.Where(Col<Friendship>::FromUserId == TargetId)
		.Where(Col<Friendship>::Status == std::string(FriendStatus::Accepted))
		.ToList();

	auto friendList2 = dbContext.Set<Friendship>()
		.Where(Col<Friendship>::FromUserId == MyUserId)
		.Where(Col<Friendship>::ToUserId == TargetId)
		.Where(Col<Friendship>::Status == std::string(FriendStatus::Accepted))
		.ToList();

	if (friendList1.empty() && friendList2.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Not a friend");
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// delete
	for (auto& f : friendList1)
	{
		dbContext.Set<Friendship>().Delete(f);
	}
	
	for (auto& f : friendList2)
	{
		dbContext.Set<Friendship>().Delete(f);
	}
	
	if (!dbContext.SaveChanges())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Database error");
		spdlog::error("Failed to remove friendship: {} <-> {}", MyUserId, TargetId);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	ResPkt.set_success(true);
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	spdlog::info("Friend removed: {} <-> {}", MyUserId, TargetId);
	return true;
}
