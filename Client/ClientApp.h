#pragma once

#include "Types.h"
#include "Packet/SendBuffer.h"
#include "ClientSession.h"
#include <mutex>

// 클라이언트 현재 위치. Auth/Lobby/Chat/MyPage 사이를 자유롭게 오갈 수 있도록 상태로 관리한다.
enum class ClientState
{
	Auth,
	Lobby,
	Chat,
	MyPage,
	Friend,
	Exit,
};

// 응답 플래그가 true가 될 때까지 100ms 단위로 폴링. timeout 경과 시 false 반환.
// 각 루프에서 요청 송신 후 대기하는 공통 패턴을 한 줄로 축약.
bool WaitForResponse(Atomic<bool>& bDone, int32 iTimeoutMs = 5000);

// 로그인 응답 수신 상태. AuthLoop가 요청 후 대기하고, 수신 핸들러가 세팅한다.
extern Atomic<bool> GLoginDone;
extern Atomic<bool> GLoginSuccess;

// 서버에서 수신한 방 목록의 개별 항목.
struct RoomInfo
{
	int32  RoomId;
	String RoomName;
};

// 서버에서 수신한 친구/요청 목록의 개별 항목.
struct FriendInfo
{
	String Email;
	String Nickname;
};

// 방 리스트 수신 상태. LobbyLoop가 요청 후 대기하고, 수신 핸들러가 채운다.
extern Atomic<bool>     GRoomListDone;
extern std::mutex       GRoomListMutex;
extern Vector<RoomInfo> GRoomList;

// 방 생성 상태
extern Atomic<bool>		GCreateRoomDone;
extern Atomic<bool> 	GCreateRoomSuccess;
extern int32			GCreatedRoomId;

extern Atomic<bool>		GExitRoomDone;

extern Atomic<bool> GUpdateNicknameDone;
extern Atomic<bool> GUpdateNicknameSuccess;

extern Atomic<bool> GDeleteAccountDone;
extern Atomic<bool> GDeleteAccountSuccess;

// 친구 목록 수신 상태.
extern Atomic<bool>        GFriendListDone;
extern std::mutex          GFriendListMutex;
extern Vector<FriendInfo>  GFriendList;

// 받은 친구 요청 목록 수신 상태.
extern Atomic<bool>        GPendingFriendsDone;
extern std::mutex          GPendingFriendsMutex;
extern Vector<FriendInfo>  GPendingFriends;

// 요청/수락/거절/삭제 응답 상태.
extern Atomic<bool>        GRequestFriendDone;
extern Atomic<bool>        GRequestFriendSuccess;
extern Atomic<bool>        GAcceptFriendDone;
extern Atomic<bool>        GAcceptFriendSuccess;
extern Atomic<bool>        GRejectFriendDone;
extern Atomic<bool>        GRejectFriendSuccess;
extern Atomic<bool>        GRemoveFriendDone;
extern Atomic<bool>        GRemoveFriendSuccess;


// 클라이언트 애플리케이션 최상위 클래스.
// 서버 연결, 회원가입/로그인, 채팅 흐름을 관리한다.
class ClientApp
{
public:
	ClientApp();
	~ClientApp();

	void Run();

private:
	void AuthLoop();
	void LobbyLoop();
	void ChatLoop();
	void MyPageLoop();
	void FriendLoop();

	SendBufferManager SendBufferManagerInstance_;
	IoContext Context_;
	SharedPtr<ClientSession> SessionPtr_;

	ClientState State_ = ClientState::Auth;
};