#include "ServerPacketHandler.h"
#include "ClientApp.h"
#include "Session.h"
#include <spdlog/spdlog.h>
#include <iostream>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

Atomic<bool> GRegisterDone{false};
Atomic<bool> GRegisterSuccess{false};
String       GRegisterMessage;

Atomic<bool> GLoginDone{false};
Atomic<bool> GLoginSuccess{false};
String       GLoginMessage;

Atomic<bool>     GRoomListDone{false};
std::mutex       GRoomListMutex;
Vector<RoomInfo> GRoomList;

Atomic<bool>	GCreateRoomDone{false};
Atomic<bool> 	GCreateRoomSuccess{false};
int32			GCreatedRoomId;

Atomic<bool>	GExitRoomDone{false};

Atomic<bool> GUpdateNicknameDone{false};
Atomic<bool> GUpdateNicknameSuccess{false};
String       GUpdateNicknameMessage;

Atomic<bool> GDeleteAccountDone{false};
Atomic<bool> GDeleteAccountSuccess{false};
String       GDeleteAccountMessage;

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
String             GFriendActionMessage;

// 회원가입 결과 수신. 결과 메시지는 AuthLoop 가 화면에 출력하므로 여기서는 플래그만 세팅.
bool Handle_S_REGISTER(SharedPtr<Session> SessionPtr, Protocol::S_REGISTER& Pkt)
{
	GRegisterSuccess = Pkt.success();
	GRegisterMessage = Pkt.msg();
	GRegisterDone = true;
	return true;
}

// 로그인 결과 수신. 성공 시 본인 닉네임을 GMyNickname 에 저장 (좌/우 정렬용).
// 결과 메시지는 AuthLoop 가 출력.
bool Handle_S_LOGIN(SharedPtr<Session> SessionPtr, Protocol::S_LOGIN& Pkt)
{
	if (Pkt.success())
		GMyNickname = Pkt.name();

	GLoginMessage = Pkt.msg();
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

// 방 생성 결과를 수신한다. UI 출력은 LobbyLoop 가 책임진다.
bool Handle_S_CREATE_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_CREATE_ROOM& Pkt)
{
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

// 방 입장 결과 수신. ChatLoop 헤더가 방 정보를 보여주므로 별도 출력 없음.
bool Handle_S_ENTER_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_ENTER_ROOM& Pkt)
{
	return true;
}

// 채팅 메시지 수신. 본인이 보낸 echo 면 오른쪽 정렬(닉네임 뒤), 타인이면 왼쪽(닉네임 앞).
bool Handle_S_CHAT(SharedPtr<Session> SessionPtr, Protocol::S_CHAT& Pkt)
{
	const bool bIsMine = (Pkt.name() == GMyNickname);
	if (bIsMine)
		PrintChatMessage(Pkt.msg() + " [" + Pkt.name() + "]", true);
	else
		PrintChatMessage("[" + Pkt.name() + "] " + Pkt.msg(), false);
	return true;
}

// 확성기 메시지 수신. 주황색(#208) 유지하면서 본인/타인 정렬 분기.
bool Handle_S_SHOUT(SharedPtr<Session> SessionPtr, Protocol::S_SHOUT& Pkt)
{
	const bool bIsMine = (Pkt.name() == GMyNickname);
	if (bIsMine)
		PrintChatMessage("\033[38;5;208m" + Pkt.msg() + " [확성기 " + Pkt.name() + "]\033[0m", true);
	else
		PrintChatMessage("\033[38;5;208m[확성기 " + Pkt.name() + "] " + Pkt.msg() + "\033[0m", false);
	return true;
}

// 귓속말 수신. 성공 시 하늘색(#36), 실패 시 빨강(#31).
// 받은 귓속말은 항상 타인이므로 왼쪽 정렬. 본인 발신 에코는 클라 ChatLoop 가 직접 처리.
bool Handle_S_WHISPER(SharedPtr<Session> SessionPtr, Protocol::S_WHISPER& Pkt)
{
	if (!Pkt.success())
	{
		PrintChatMessage("\033[31m[귓속말] " + Pkt.error_msg() + "\033[0m", false);
		return true;
	}

	PrintChatMessage("\033[36m[귓속말 ← " + Pkt.from_name() + "] " + Pkt.message() + "\033[0m", false);
	return true;
}

// 닉네임 변경 결과 수신. UI 출력은 MyPageLoop 가 책임진다.
bool Handle_S_UPDATE_NICKNAME(SharedPtr<Session> SessionPtr, Protocol::S_UPDATE_NICKNAME& Pkt)
{
	GUpdateNicknameSuccess = Pkt.success();
	GUpdateNicknameMessage = Pkt.msg();
	GUpdateNicknameDone    = true;
	return true;
}

bool Handle_S_DELETE_ACCOUNT(SharedPtr<Session> SessionPtr, Protocol::S_DELETE_ACCOUNT& Pkt)
{
	GDeleteAccountSuccess = Pkt.success();
	GDeleteAccountMessage = Pkt.msg();
	GDeleteAccountDone    = true;
	return true;
}

// 친구 요청 결과 수신. 메시지는 GFriendActionMessage 에 저장(FriendLoop 가 출력).
bool Handle_S_REQUEST_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REQUEST_FRIEND& Pkt)
{
	GRequestFriendSuccess = Pkt.success();
	GFriendActionMessage = Pkt.msg();
	GRequestFriendDone = true;
	return true;
}

bool Handle_S_ACCEPT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_ACCEPT_FRIEND& Pkt)
{
	GAcceptFriendSuccess = Pkt.success();
	GFriendActionMessage = Pkt.msg();
	GAcceptFriendDone = true;
	return true;
}

bool Handle_S_REJECT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REJECT_FRIEND& Pkt)
{
	GRejectFriendSuccess = Pkt.success();
	GFriendActionMessage = Pkt.msg();
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

bool Handle_S_REMOVE_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REMOVE_FRIEND& Pkt)
{
	GRemoveFriendSuccess = Pkt.success();
	GFriendActionMessage = Pkt.msg();
	GRemoveFriendDone = true;
	return true;
}
