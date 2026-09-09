#include "ServerPacketHandler.h"
#include "ClientApp.h"
#include "ConsoleUI.h"
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

Atomic<bool>       GBalanceDone{false};
Atomic<bool>       GBalanceSuccess{false};
Atomic<int64>      GMyBalance{0};

Atomic<bool>       GTransferDone{false};
Atomic<bool>       GTransferSuccess{false};
String             GTransferMessage;
Atomic<int32>      GTransferRetries{0};

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

// ==========================
// 포인트 (잔고 조회 / 이체)
// ==========================

// 잔고 조회 결과. MyPageLoop 가 헤더에 표시.
bool Handle_S_GET_BALANCE(SharedPtr<Session> SessionPtr, Protocol::S_GET_BALANCE& Pkt)
{
	GBalanceSuccess = Pkt.success();
	if (Pkt.success())
		GMyBalance = Pkt.balance();
	GBalanceDone = true;
	return true;
}

// 이체 결과. 성공 시 서버가 돌려준 잔고로 동기화.
bool Handle_S_TRANSFER(SharedPtr<Session> SessionPtr, Protocol::S_TRANSFER& Pkt)
{
	GTransferSuccess = Pkt.success();
	GTransferMessage = Pkt.msg();
	GTransferRetries = Pkt.retries();
	if (Pkt.success())
		GMyBalance = Pkt.my_balance();
	GTransferDone = true;
	return true;
}

// 다른 유저가 나에게 이체했을 때 서버가 push 하는 알림. 어느 화면에 있든 한 줄 출력.
bool Handle_S_TRANSFER_RECEIVED(SharedPtr<Session> SessionPtr, Protocol::S_TRANSFER_RECEIVED& Pkt)
{
	GMyBalance = Pkt.my_balance();
	PrintChatMessage("\033[32m[포인트] " + Pkt.from_name() + " 님이 "
		+ std::to_string(Pkt.amount()) + " P 를 보냈습니다. (잔고 "
		+ std::to_string(Pkt.my_balance()) + " P)\033[0m", false);
	return true;
}

// ==========================
// AI 채팅 (스트리밍 수신)
// ==========================

namespace
{
	std::mutex GAiMutex;
	String     GAiPending;          // 아직 확정되지 않은 줄 (조각이 올 때마다 여기에 붙고, 화면에는 부분 줄로 그려진다)
	bool       GAiShown = false;    // GAiPending 이 화면 맨 아래 행에 부분 줄로 그려져 있는지
	bool       GAiFirstLine = true; // 답변의 첫 줄에만 [AI] 태그, 이후 줄은 들여쓰기

	String AiLineText(const String& Line)
	{
		// 답변 본문은 기본색(흰색). [AI] 태그만 굵게 — 자주색은 검정 배경에서 읽기 어려웠다.
		return (GAiFirstLine ? String("\033[1m[AI]\033[0m ") : String("     ")) + Line;
	}

	// 줄 하나를 확정한다. 부분 줄이 그려져 있으면 그 행을 최종 텍스트로 덮어쓰고, 아니면 새 행에 찍는다.
	void FlushAiLine(const String& Line)
	{
		const String Text = AiLineText(Line);
		if (!DrawAiLine(Text, true))
			PrintChatMessage(Text, false);   // ChatLoop 밖
		GAiShown     = false;
		GAiFirstLine = false;
	}

	// UTF-8 경계를 지켜 앞부분을 잘라낸다. 공백이 있으면 거기서, 없으면 바이트 상한에서.
	String CutFront(String& Buf, int32 iMaxWidth)
	{
		size_t cut = Buf.rfind(' ');
		if (cut == String::npos || cut == 0 || ConsoleUI::DisplayWidth(Buf.substr(0, cut)) > iMaxWidth)
		{
			cut = std::min<size_t>(Buf.size(), 120);
			while (cut > 0 && (static_cast<unsigned char>(Buf[cut]) & 0xC0) == 0x80) --cut;
		}
		String Head = Buf.substr(0, cut);
		Buf.erase(0, (cut < Buf.size() && Buf[cut] == ' ') ? cut + 1 : cut);
		return Head;
	}
}

// 서버가 API 의 텍스트 조각을 받는 대로 보내준다. 조각이 올 때마다:
//   1) 줄바꿈이 있거나 한 줄 폭을 넘으면 그 부분은 완성 줄로 확정하고
//   2) 남은 부분 줄은 맨 아래 행에 즉시 다시 그린다 → 글자가 타이핑되듯 늘어난다.
bool Handle_S_AI_CHAT(SharedPtr<Session> SessionPtr, Protocol::S_AI_CHAT& Pkt)
{
	std::lock_guard<std::mutex> Lock(GAiMutex);
	const int32 iMaxWidth = std::max(30, ConsoleUI::GetSize().Width - 12);

	// 부분 줄을 그려 둔 사이에 다른 메시지가 끼어들었으면 그 줄은 이미 위로 밀려 확정된 셈이다.
	// 화면에 나간 텍스트는 버리고 다음 조각부터 새 행에 이어 쓴다 (문장이 두 줄로 갈라질 수 있다).
	if (GAiShown && !IsBottomLineAi())
	{
		GAiPending.clear();
		GAiShown     = false;
		GAiFirstLine = false;
	}

	GAiPending += Pkt.text();

	size_t nl;
	while ((nl = GAiPending.find('\n')) != String::npos)
	{
		FlushAiLine(GAiPending.substr(0, nl));
		GAiPending.erase(0, nl + 1);
	}
	while (ConsoleUI::DisplayWidth(GAiPending) > iMaxWidth)
		FlushAiLine(CutFront(GAiPending, iMaxWidth));

	if (Pkt.done())
	{
		if (!GAiPending.empty())
			FlushAiLine(GAiPending);
		GAiPending.clear();
		GAiShown = false;
		if (!Pkt.success())
			PrintChatMessage("\033[31m[AI] 오류: " + Pkt.error_msg() + "\033[0m", false);
		GAiFirstLine = true;
		return true;
	}

	// 아직 안 끝난 줄을 지금 상태로 다시 그린다
	if (!GAiPending.empty())
		GAiShown = DrawAiLine(AiLineText(GAiPending), false);
	return true;
}
