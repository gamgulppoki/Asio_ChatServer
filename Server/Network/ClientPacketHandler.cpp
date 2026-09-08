#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "Room.h"
#include "RoomManager.h"
#include "../ServerGlobal.h"
#include "../DB/DBConnectionPool.h"
#include "../Security/InputValidator.h"
#include "../Security/PasswordHasher.h"
#include "StringUtils.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "SessionManager.h"
#include "DB/Entities/Entities.h"
#include "DB/ORM/DBContext.h"
#include "DB/Generated/EntitiesGenerated.h"

namespace
{
	// 회원가입 시 지급하는 초기 포인트. 이체 데모용 시드값.
	constexpr int64 kInitialBalance = 10000;

	// 이체 1건 상한. 입력 오류와 int64 오버플로 방지.
	constexpr int64 kMaxTransferAmount = 1'000'000'000;

	// OCC 충돌 시 재시도 상한. 넘으면 실패로 응답한다 (무한 루프·기아 방지).
	constexpr int32 kMaxTransferRetry = 3;

	// 이체 동시성 제어 방식.
	//   OCC     — 락 없이 읽고 UPDATE 시 origin 비교로 충돌 감지, 충돌하면 재시도 (낙관적)
	//   UpdLock — 트랜잭션을 먼저 열고 두 행을 UPDLOCK 으로 읽어 Commit 까지 독점 (비관적)
	// 환경 변수 CHATSERVER_TRANSFER_LOCK = occ | updlock (기본 occ).
	// 부하 테스트에서 재빌드 없이 두 방식을 같은 코드 위에서 비교하기 위한 스위치.
	enum class TransferLockMode { OCC, UpdLock };

	TransferLockMode GetTransferLockMode()
	{
		static const TransferLockMode Mode = []
		{
			char Env[32] = {};
			size_t Len = 0;
			getenv_s(&Len, Env, sizeof(Env), "CHATSERVER_TRANSFER_LOCK");   // MSVC 안전 버전 (없으면 빈 문자열)
			const bool bUpdLock = _stricmp(Env, "updlock") == 0;
			spdlog::info("[Transfer] lock mode = {}", bUpdLock ? "UPDLOCK (pessimistic)" : "OCC (optimistic)");
			return bUpdLock ? TransferLockMode::UpdLock : TransferLockMode::OCC;
		}();
		return Mode;
	}
}

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

	// 비밀번호는 bcrypt 해시로만 저장한다. 평문은 이 핸들러를 벗어나지 않는다.
	const std::string PasswordHash = PasswordHasher::Hash(Pkt.password());
	if (PasswordHash.empty())
	{
		ResPkt.set_success(false);
		ResPkt.set_msg("Server error");
		spdlog::error("Password hashing failed for {}", Pkt.email());
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	// User 구조체 채우기
	newUserPtr->Nickname = Pkt.name();
	newUserPtr->Email = Pkt.email();
	newUserPtr->Password = PasswordHash;
	newUserPtr->Balance = kInitialBalance;

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

	// 비밀번호 검증 (bcrypt)
	const std::string& Stored = FoundUser->Password.value();
	bool bPasswordOk = false;
	if (PasswordHasher::IsHash(Stored))
	{
		bPasswordOk = PasswordHasher::Verify(Pkt.password(), Stored);
	}
	else
	{
		// 해싱 도입 전에 만들어진 평문 행. 맞으면 이번 로그인에서 해시로 교체한다 (점진적 마이그레이션).
		// 전체 행을 한 번에 바꿀 수 없는 이유: 평문을 모르면 해시를 만들 수 없다. 로그인 순간에만 평문을 안다.
		bPasswordOk = (Stored == Pkt.password());
		if (bPasswordOk)
		{
			const std::string Upgraded = PasswordHasher::Hash(Pkt.password());
			if (!Upgraded.empty())
			{
				FoundUser->Password = Upgraded;
				if (dbContext.SaveChanges())
					spdlog::info("Password upgraded to bcrypt for {}", Pkt.email());
			}
		}
	}

	if (!bPasswordOk)
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
	// NOLOCK: 목록 화면은 잠깐 틀려도 된다 (다음 진입 때 다시 읽음). 락 대기 없이 바로 응답.
	auto pendings = dbContext.Set<Friendship>()
		.WithHint(Hint::NoLock)
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

	// NOLOCK: 친구 목록은 잠깐 틀려도 되는 조회. 수락/삭제 트랜잭션과 겹쳐도 기다리지 않는다.
	auto friendList1 = dbContext.Set<Friendship>()
		.WithHint(Hint::NoLock)
		.Include(&Friendship::ToUser)
		.Where(Col<Friendship>::FromUserId == MyUserId)
		.Where(Col<Friendship>::Status   == std::string(FriendStatus::Accepted))
		.ToList();

	auto friendList2 = dbContext.Set<Friendship>()
		.WithHint(Hint::NoLock)
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

// ==========================
// 포인트 (잔고 조회 / 이체)
// ==========================

// 본인 잔고 조회. 마이페이지 진입 시 호출된다.
bool Handle_C_GET_BALANCE(SharedPtr<Session> SessionPtr, Protocol::C_GET_BALANCE& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_GET_BALANCE ResPkt;

	const uint64 UserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (UserId == 0)
	{
		ResPkt.set_success(false);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	DBConnectionScope Scope(GDBPool);
	DBContext dbContext;
	dbContext.SetDBConnection(Scope.Get());

	auto Users = dbContext.Set<User>().Where(Col<User>::Id == static_cast<int64>(UserId)).ToList();
	if (Users.empty())
	{
		ResPkt.set_success(false);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	}

	ResPkt.set_success(true);
	ResPkt.set_balance(Users.front()->Balance.value());
	GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}

// 포인트 이체. 원장의 기본 동작을 축소한 시나리오:
//   두 계좌를 읽고 → 잔고를 검증하고 → 두 행을 한 트랜잭션에서 갱신한다.
//
// 동시성 (GetTransferLockMode 로 선택):
//   OCC     — 락 없이 읽는다. SaveChanges 의 UPDATE WHERE 절이 로드 시점 값 전체를 매칭하므로
//             같은 계좌를 다른 요청이 먼저 바꿨다면 rowCount 0 → 전체 Rollback → 여기서 재시도.
//             매 시도마다 DBContext 를 새로 만들어 두 계좌를 DB 에서 다시 읽는다 (Identity Map 초기화).
//   UpdLock — 트랜잭션을 먼저 열고 두 행을 WITH (UPDLOCK, ROWLOCK) 으로 읽는다. Commit 까지 남이
//             못 고치므로 충돌 자체가 없고, 경쟁 요청은 실패 대신 대기한다. 잔고처럼 실패 비용이 큰 곳용.
// 데드락 방지:
//   OCC     — 두 UPDATE 는 Identity Map 순회 순서(= PK 오름차순)로 나간다.
//   UpdLock — 두 SELECT 를 PK 오름차순으로 한다.
//   어느 쪽이든 A→B 와 B→A 가 동시에 와도 락 획득 순서가 같아 교착이 생기지 않는다.
bool Handle_C_TRANSFER(SharedPtr<Session> SessionPtr, Protocol::C_TRANSFER& Pkt)
{
	auto GameSessionPtr = std::static_pointer_cast<GameSession>(SessionPtr);
	Protocol::S_TRANSFER ResPkt;

	auto Fail = [&](const char* Msg)
	{
		ResPkt.set_success(false);
		ResPkt.set_msg(Msg);
		GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
		return true;
	};

	// 입력 검증
	const uint64 UserId = GameSessionPtr->GetPlayerInfo().PlayerId;
	if (UserId == 0)
		return Fail("Not logged in");

	const int64 Amount = Pkt.amount();
	if (Amount <= 0 || Amount > kMaxTransferAmount)
		return Fail("Invalid amount");

	if (!InputValidator::IsValidName(Pkt.target_nickname()))
		return Fail("Invalid nickname");

	const std::string MyNickname = StringUtils::WideToUtf8(GameSessionPtr->GetPlayerInfo().Nickname);
	if (Pkt.target_nickname() == MyNickname)
		return Fail("Cannot transfer to yourself");

	DBConnectionScope Scope(GDBPool);
	const TransferLockMode LockMode = GetTransferLockMode();
	const int64 MyId = static_cast<int64>(UserId);

	for (int32 Retry = 0; Retry <= kMaxTransferRetry; ++Retry)
	{
		DBContext dbContext;
		dbContext.SetDBConnection(Scope.Get());

		User* From = nullptr;
		User* To   = nullptr;

		if (LockMode == TransferLockMode::UpdLock)
		{
			// 1-a. 대상 Id 조회는 별도 컨텍스트로 (같은 컨텍스트면 Identity Map 이
			//      락 없이 읽은 객체를 재사용해서, 아래 UPDLOCK 조회 결과를 버리게 된다)
			int64 ToId = 0;
			{
				DBContext Lookup;
				Lookup.SetDBConnection(Scope.Get());
				auto Found = Lookup.Set<User>().Where(Col<User>::Nickname == Pkt.target_nickname()).ToList();
				if (Found.empty())
					return Fail("Target not found");
				ToId = Found.front()->Id.value();
			}
			if (ToId == MyId)
				return Fail("Cannot transfer to yourself");

			// 1-b. 트랜잭션을 먼저 열고 PK 오름차순으로 UPDLOCK 조회 → Commit 까지 두 행 독점
			if (!dbContext.BeginTransaction())
				return Fail("Database error");

			const int64 LowId  = std::min(MyId, ToId);
			const int64 HighId = std::max(MyId, ToId);
			auto LowRows  = dbContext.Set<User>().WithHint(Hint::UpdLock).Where(Col<User>::Id == LowId).ToList();
			auto HighRows = dbContext.Set<User>().WithHint(Hint::UpdLock).Where(Col<User>::Id == HighId).ToList();
			if (LowRows.empty() || HighRows.empty())
				return Fail("User not found");            // dbContext 소멸자가 Rollback

			From = (LowId == MyId) ? LowRows.front() : HighRows.front();
			To   = (LowId == MyId) ? HighRows.front() : LowRows.front();
		}
		else
		{
			// 1. 두 계좌 조회 (락 없음, 매 시도마다 최신 값)
			auto FromUsers = dbContext.Set<User>().Where(Col<User>::Id == MyId).ToList();
			auto ToUsers   = dbContext.Set<User>().Where(Col<User>::Nickname == Pkt.target_nickname()).ToList();
			if (FromUsers.empty())
				return Fail("User not found");
			if (ToUsers.empty())
				return Fail("Target not found");

			From = FromUsers.front();
			To   = ToUsers.front();
			if (From->Id.value() == To->Id.value())
				return Fail("Cannot transfer to yourself");
		}

		// 2. 잔고 검증 (읽은 시점 기준. 이후 바뀌었다면 OCC 가 잡는다)
		if (From->Balance.value() < Amount)
			return Fail("Insufficient balance");

		// 3. 두 행 갱신 (Property 가 dirty 마킹)
		From->Balance -= Amount;
		To->Balance   += Amount;

		// 4. 한 트랜잭션으로 flush
		if (dbContext.SaveChanges())
		{
			ResPkt.set_success(true);
			ResPkt.set_msg("Transfer complete");
			ResPkt.set_my_balance(From->Balance.value());
			ResPkt.set_retries(Retry);
			GameSessionPtr->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
			spdlog::info("Transfer {} -> {} : {} (retries={})", MyNickname, Pkt.target_nickname(), Amount, Retry);

			// 수신자가 온라인이면 push 알림
			if (auto TargetSession = GSessionManager->GetSession(static_cast<uint64>(To->Id.value())))
			{
				Protocol::S_TRANSFER_RECEIVED Noti;
				Noti.set_from_name(MyNickname);
				Noti.set_amount(Amount);
				Noti.set_my_balance(To->Balance.value());
				TargetSession->Send(ClientPacketHandler::MakeSendBuffer(Noti));
			}
			return true;
		}

		// 5. 실패 분기: OCC 충돌이면 재시도, 그 외 DB 오류는 즉시 실패
		if (!dbContext.HadOCCConflict())
		{
			spdlog::error("Transfer DB error: {} -> {} : {}", MyNickname, Pkt.target_nickname(), Amount);
			return Fail("Database error");
		}
		if (Retry < kMaxTransferRetry)
			spdlog::warn("Transfer OCC conflict: {} -> {} : {} (retrying {}/{})",
				MyNickname, Pkt.target_nickname(), Amount, Retry + 1, kMaxTransferRetry);
		else
			spdlog::warn("Transfer OCC conflict: {} -> {} : {} (giving up after {} retries)",
				MyNickname, Pkt.target_nickname(), Amount, kMaxTransferRetry);
	}

	return Fail("Transfer failed after retries. Please try again.");
}
