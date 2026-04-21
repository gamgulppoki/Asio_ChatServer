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
	Exit,
};

// 로그인 응답 수신 상태. AuthLoop가 요청 후 대기하고, 수신 핸들러가 세팅한다.
extern Atomic<bool> GLoginDone;
extern Atomic<bool> GLoginSuccess;

// 서버에서 수신한 방 목록의 개별 항목.
struct RoomInfo
{
	int32  RoomId;
	String RoomName;
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

	SendBufferManager SendBufferManagerInstance_;
	IoContext Context_;
	SharedPtr<ClientSession> SessionPtr_;

	ClientState State_ = ClientState::Auth;
};