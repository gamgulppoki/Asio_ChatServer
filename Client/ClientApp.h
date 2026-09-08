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

// 채팅 입력 모드. Tab 키로 Normal → Shout → Whisper → AI → Normal 순환.
// AI 모드 입력은 C_AI_CHAT 으로 서버에 가고, 응답은 S_AI_CHAT 조각으로 스트리밍된다.
enum class ChatMode { Normal, Shout, Whisper, AI };

// ChatLoop이 소유하는 입력 상태. 메인 스레드(입력 루프)와 IO 스레드(서버 메시지 수신)가
// 모두 콘솔에 쓰기 때문에 GChatMutex로 직렬화한다.
extern std::mutex   GChatMutex;
extern bool         GChatActive;   // ChatLoop 안에 있을 때만 프롬프트 관리 활성화
extern ChatMode     GChatMode;
extern std::wstring GChatInput;

// 수신 핸들러용 출력. ChatLoop 안:
//   - bIsMine = true  -> 오른쪽 정렬 (본인 발신)
//   - bIsMine = false -> 왼쪽 들여쓰기 (타인 수신)
// ChatLoop 밖이면 정렬 무시하고 그냥 한 줄 출력.
void PrintChatMessage(const String& Line, bool bIsMine = false);

// 회원가입 응답 수신 상태. AuthLoop가 요청 후 대기, 수신 핸들러가 세팅.
extern Atomic<bool> GRegisterDone;
extern Atomic<bool> GRegisterSuccess;
extern String       GRegisterMessage;

// 로그인 응답 수신 상태. AuthLoop가 요청 후 대기하고, 수신 핸들러가 세팅한다.
extern Atomic<bool> GLoginDone;
extern Atomic<bool> GLoginSuccess;
extern String       GLoginMessage;

// 본인 닉네임. 로그인 응답에서 세팅, 닉네임 변경 시 동기화.
// 채팅 메시지 좌/우 정렬(본인=오른쪽) 판단에 쓰임.
extern String GMyNickname;

// 현재 입장한 방 정보. LobbyLoop에서 입장 직전에 세팅, ChatLoop 헤더 박스에 표시.
extern int32  GCurrentRoomId;
extern String GCurrentRoomName;

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
	bool IsOnline;
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
extern String       GUpdateNicknameMessage;

extern Atomic<bool> GDeleteAccountDone;
extern Atomic<bool> GDeleteAccountSuccess;
extern String       GDeleteAccountMessage;

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
// 친구 액션 결과 메시지(공통). 한 번에 한 액션만 진행되므로 단일 변수로 충분.
extern String              GFriendActionMessage;

// 잔고 조회 응답 상태. MyPageLoop 진입 시 요청, 수신 핸들러가 세팅.
// GMyBalance 는 이체 응답 / 수신 알림에서도 갱신된다.
extern Atomic<bool>        GBalanceDone;
extern Atomic<bool>        GBalanceSuccess;
extern Atomic<int64>       GMyBalance;

// 이체 응답 상태.
extern Atomic<bool>        GTransferDone;
extern Atomic<bool>        GTransferSuccess;
extern String              GTransferMessage;
extern Atomic<int32>       GTransferRetries;   // 서버가 OCC 충돌로 재시도한 횟수


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