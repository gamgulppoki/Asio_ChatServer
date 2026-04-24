#include "ServerPacketHandler.h"
#include "ClientApp.h"
#include "Session.h"
#include <spdlog/spdlog.h>
#include <iostream>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

Atomic<bool> GLoginDone{false};
Atomic<bool> GLoginSuccess{false};

Atomic<bool>     GRoomListDone{false};
std::mutex       GRoomListMutex;
Vector<RoomInfo> GRoomList;

Atomic<bool>	GCreateRoomDone{false};
Atomic<bool> 	GCreateRoomSuccess{false};
int32			GCreatedRoomId;

Atomic<bool>	GExitRoomDone{false};

Atomic<bool> GUpdateNicknameDone{false};
Atomic<bool> GUpdateNicknameSuccess{false};

Atomic<bool> GDeleteAccountDone{false};
Atomic<bool> GDeleteAccountSuccess{false};

Atomic<bool>       GFriendListDone{false};
std::mutex         GFriendListMutex;
Vector<FriendInfo> GFriendList;

Atomic<bool>       GPendingFriendsDone{false};
std::mutex         GPendingFriendsMutex;
Vector<FriendInfo> GPendingFriends;

Atomic<bool>       GRequestFriendDone{false};
Atomic<bool>       GRequestFriendSuccess{false};
Atomic<bool>       GAcceptFriendDone{false};
Atomic<bool>       GAcceptFriendSuccess{false};
Atomic<bool>       GRejectFriendDone{false};
Atomic<bool>       GRejectFriendSuccess{false};
Atomic<bool>       GRemoveFriendDone{false};
Atomic<bool>       GRemoveFriendSuccess{false};

// 회원가입 결과를 수신한다.
bool Handle_S_REGISTER(SharedPtr<Session> SessionPtr, Protocol::S_REGISTER& Pkt)
{
	if (Pkt.success())
		std::cout << "[Register] Success." << std::endl;
	else
		std::cout << "[Register] Failed: " << Pkt.msg() << std::endl;

	return true;
}

// 로그인 결과를 수신한다.
bool Handle_S_LOGIN(SharedPtr<Session> SessionPtr, Protocol::S_LOGIN& Pkt)
{
	if (Pkt.success())
		std::cout << "[Login] Welcome, " << Pkt.name() << "!" << std::endl;
	else
		std::cout << "[Login] Failed: " << Pkt.msg() << std::endl;

	GLoginSuccess = Pkt.success();
	GLoginDone = true;
	return true;
}

// 등록되지 않은 패킷 ID가 들어왔을 때 호출된다.
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
{
	PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
	spdlog::warn("Unknown packet id: {}", Header->iId);
	return false;
}

// 방 생성 결과를 수신한다.
bool Handle_S_CREATE_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_CREATE_ROOM& Pkt)
{
	if (Pkt.success())
	{
		std::cout << "Room Created." << std::endl;
	}
	
	GCreateRoomSuccess = Pkt.success();
	GCreatedRoomId = Pkt.roomid();
	GCreateRoomDone = true;
	
	return true;
}

// 방 리스트를 수신한다.
bool Handle_S_GET_ROOM_LIST(SharedPtr<Session> SessionPtr, Protocol::S_GET_ROOM_LIST& Pkt)
{
	if (Pkt.success())
	{
		std::lock_guard<std::mutex> Lock(GRoomListMutex);
		GRoomList.clear();
		GRoomList.reserve(Pkt.rooms_size());
		for (const auto& r : Pkt.rooms())
		{
			GRoomList.push_back({ r.roomid(), r.roomname() });
		}
	}
	GRoomListDone = true;
	return true;
}

// 방 퇴장 결과를 수신한다.
bool Handle_S_EXIT_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_EXIT_ROOM& Pkt)
{
	if (Pkt.success())
	{
	}
	GExitRoomDone = true;
	return true;
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
	PrintChatMessage("[" + Pkt.name() + "] " + Pkt.msg());
	return true;
}

// 서버로부터 확성기 메시지를 수신하여 출력한다. 주황색(ANSI 256색 #208)으로 강조.
bool Handle_S_SHOUT(SharedPtr<Session> SessionPtr, Protocol::S_SHOUT& Pkt)
{
	PrintChatMessage("\033[38;5;208m[확성기][" + Pkt.name() + "] " + Pkt.msg() + "\033[0m");
	return true;
}

// 닉네임 변경 결과를 수신한다.
bool Handle_S_UPDATE_NICKNAME(SharedPtr<Session> SessionPtr, Protocol::S_UPDATE_NICKNAME& Pkt)
{
	if (Pkt.success())
		std::cout << "[Nickname] Updated successfully." << std::endl;
	else
		std::cout << "[Nickname] Failed: " << Pkt.msg() << std::endl;

	GUpdateNicknameSuccess = Pkt.success();
	GUpdateNicknameDone    = true;
	return true;
}

// 계정 탈퇴 결과를 수신한다.
bool Handle_S_DELETE_ACCOUNT(SharedPtr<Session> SessionPtr, Protocol::S_DELETE_ACCOUNT& Pkt)
{
	if (Pkt.success())
		std::cout << "[Account] Deleted." << std::endl;
	else
		std::cout << "[Account] Failed: " << Pkt.msg() << std::endl;

	GDeleteAccountSuccess = Pkt.success();
	GDeleteAccountDone    = true;
	return true;
}

// 친구 요청 결과 수신.
bool Handle_S_REQUEST_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REQUEST_FRIEND& Pkt)
{
	if (!Pkt.success())
		std::cout << "[Friend] Request failed: " << Pkt.msg() << std::endl;

	GRequestFriendSuccess = Pkt.success();
	GRequestFriendDone = true;
	return true;
}

// 친구 요청 수락 결과 수신.
bool Handle_S_ACCEPT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_ACCEPT_FRIEND& Pkt)
{
	if (!Pkt.success())
		std::cout << "[Friend] Accept failed: " << Pkt.msg() << std::endl;

	GAcceptFriendSuccess = Pkt.success();
	GAcceptFriendDone = true;
	return true;
}

// 친구 요청 거절 결과 수신.
bool Handle_S_REJECT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REJECT_FRIEND& Pkt)
{
	if (!Pkt.success())
		std::cout << "[Friend] Reject failed: " << Pkt.msg() << std::endl;

	GRejectFriendSuccess = Pkt.success();
	GRejectFriendDone = true;
	return true;
}

// 받은 친구 요청 목록 수신.
bool Handle_S_GET_PENDING_FRIENDS(SharedPtr<Session> SessionPtr, Protocol::S_GET_PENDING_FRIENDS& Pkt)
{
	if (Pkt.success())
	{
		std::lock_guard<std::mutex> Lock(GPendingFriendsMutex);
		GPendingFriends.clear();
		GPendingFriends.reserve(Pkt.pendings_size());
		for (const auto& f : Pkt.pendings())
			GPendingFriends.push_back({ f.email(), f.nickname() });
	}
	GPendingFriendsDone = true;
	return true;
}

// 친구 목록 수신.
bool Handle_S_GET_FRIEND_LIST(SharedPtr<Session> SessionPtr, Protocol::S_GET_FRIEND_LIST& Pkt)
{
	if (Pkt.success())
	{
		std::lock_guard<std::mutex> Lock(GFriendListMutex);
		GFriendList.clear();
		GFriendList.reserve(Pkt.friends_size());
		for (const auto& f : Pkt.friends())
			GFriendList.push_back({ f.email(), f.nickname(), f.is_online() });
	}
	GFriendListDone = true;
	return true;
}

// 친구 삭제 결과 수신.
bool Handle_S_REMOVE_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REMOVE_FRIEND& Pkt)
{
	if (!Pkt.success())
		std::cout << "[Friend] Remove failed: " << Pkt.msg() << std::endl;

	GRemoveFriendSuccess = Pkt.success();
	GRemoveFriendDone = true;
	return true;
}
