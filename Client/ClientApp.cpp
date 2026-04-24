#include "ClientApp.h"
#include "CoreGlobal.h"
#include "ServerPacketHandler.h"
#include "StringUtils.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <conio.h>

const int32 iPort = 9000;

namespace
{
	// 상태 전환 직전에 유저가 메시지를 충분히 읽도록 엔터 대기.
	// 다음 루프 진입 시 콘솔이 clear되므로 이 pause가 없으면 결과가 순식간에 사라짐.
	void WaitForEnter()
	{
		std::cout << "\n(엔터 키를 눌러 계속)" << std::flush;
		String Dummy;
		std::getline(std::cin, Dummy);
	}
}

// 응답 플래그가 true가 될 때까지 100ms 단위로 폴링. timeout 경과 시 false.
bool WaitForResponse(Atomic<bool>& bDone, int32 iTimeoutMs)
{
	const int32 iStepMs = 100;
	const int32 iLoops = iTimeoutMs / iStepMs;
	for (int32 i = 0; i < iLoops && !bDone; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(iStepMs));
	return bDone.load();
}

// 채팅 입력 상태 전역. ClientApp.h 선언의 정의부.
std::mutex   GChatMutex;
bool         GChatActive = false;
ChatMode     GChatMode   = ChatMode::Normal;
std::wstring GChatInput;

namespace
{
	// 현재 프롬프트 + 입력 버퍼를 현재 줄에 그린다. 반드시 GChatMutex 보호 안에서만 호출.
	void RedrawPromptLocked()
	{
		// 확성기 모드일 때만 태그를 주황색(ANSI 256색 #208)으로 강조
		const char* Tag = (GChatMode == ChatMode::Normal)
			? "[일반]"
			: "\033[38;5;208m[확성기]\033[0m";
		std::cout << "\r\033[2K" << Tag << " > "
		          << StringUtils::WideToUtf8(GChatInput) << std::flush;
	}
}

// 브로드캐스트 메시지 출력. ChatLoop 중이면 프롬프트를 잠깐 지웠다가 메시지 찍고 프롬프트 재출력.
// mutex 덕분에 입력 스레드의 Redraw와 섞이지 않는다.
void PrintChatMessage(const String& Line)
{
	std::lock_guard<std::mutex> Lock(GChatMutex);
	if (GChatActive)
	{
		std::cout << "\r\033[2K" << Line << "\n";
		RedrawPromptLocked();
	}
	else
	{
		std::cout << Line << "\n" << std::flush;
	}
}

// 전역 싱글톤 바인딩 + 패킷 핸들러 초기화
ClientApp::ClientApp()
{
	GSendBufferManager = &SendBufferManagerInstance_;
	ServerPacketHandler::Init();
}

// 전역 싱글톤 정리
ClientApp::~ClientApp()
{
	GSendBufferManager = nullptr;
}

// 서버 연결 -> 상태 기반 루프 진행 -> 종료
void ClientApp::Run()
{
	TcpSocket Socket(Context_);
	TcpResolver Resolver(Context_);
	asio::connect(Socket, Resolver.resolve("127.0.0.1", std::to_string(iPort)));

	SessionPtr_ = std::make_shared<ClientSession>(std::move(Socket));
	SessionPtr_->Start();

	// 수신 처리용 스레드
	std::thread IoThread([this]() { Context_.run(); });

	// 현재 상태에 해당하는 루프를 호출. 각 루프 내부에서 State_를 다음 상태로 전환한다.
	// State_ 변경될 때마다 콘솔을 클리어해 화면을 깔끔하게 유지한다.
	ClientState PrevState = ClientState::Exit;  // 초기 Auth와 달라서 첫 진입 시에도 클리어 트리거
	while (State_ != ClientState::Exit)
	{
		if (State_ != PrevState)
		{
			std::system("cls");
			PrevState = State_;
		}

		switch (State_)
		{
		case ClientState::Auth:   AuthLoop();   break;
		case ClientState::Lobby:  LobbyLoop();  break;
		case ClientState::Chat:   ChatLoop();   break;
		case ClientState::MyPage: MyPageLoop(); break;
		case ClientState::Friend: FriendLoop(); break;
		default: break;
		}
	}

	SessionPtr_->Disconnect();
	IoThread.join();
}

// 회원가입/로그인 메뉴
void ClientApp::AuthLoop()
{
	bool bLoggedIn = false;

	while (!bLoggedIn)
	{
		std::cout << "\n=== Welcome ===" << std::endl;
		std::cout << "1. Register" << std::endl;
		std::cout << "2. Login" << std::endl;
		std::cout << "> ";

		String Choice;
		std::getline(std::cin, Choice);

		if (Choice == "1")
		{
			String Name, Email, Password;
			std::cout << "Name: ";
			std::getline(std::cin, Name);
			std::cout << "Email: ";
			std::getline(std::cin, Email);
			std::cout << "Password: ";
			std::getline(std::cin, Password);

			Protocol::C_REGISTER Pkt;
			Pkt.set_name(Name);
			Pkt.set_email(Email);
			Pkt.set_password(Password);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			// 서버 응답 수신 대기
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
		else if (Choice == "2")
		{
			String Email, Password;
			std::cout << "Email: ";
			std::getline(std::cin, Email);
			std::cout << "Password: ";
			std::getline(std::cin, Password);

			Protocol::C_LOGIN Pkt;
			Pkt.set_email(Email);
			Pkt.set_password(Password);

			// 응답 플래그 초기화 후 송신
			GLoginDone = false;
			GLoginSuccess = false;
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			WaitForResponse(GLoginDone);

			if (GLoginSuccess)
				bLoggedIn = true;
			// 실패 시 메뉴 루프 계속
		}
	}

	WaitForEnter();
	State_ = ClientState::Lobby;
}

void ClientApp::LobbyLoop()
{
	// 방 생성 / 로비 입장 / 마이페이지
	std::cout << "1. 방 생성\n";
	std::cout << "2. 방 입장\n";
	std::cout << "3. 마이페이지\n";
	std::cout << "4. 친구 목록\n";
	std::cout << "> ";

	String MenuInput;
	std::getline(std::cin, MenuInput);

	int32 iMenuChoice = 0;
	try
	{
		iMenuChoice = std::stoi(MenuInput);
	}
	catch (const std::exception&)
	{
		std::cout << "잘못된 입력입니다." << std::endl;
		return;
	}

	int32 iTargetRoomId = 0;

	if (iMenuChoice == 1)
	{
		// 방 이름
		std::cout << "생성할 방 이름을 입력해주세요.\n";
		std::cout << "> ";

		String RoomName;
		std::getline(std::cin, RoomName);

		// 응답 플래그 리셋 후 방 생성 패킷 송신
		GCreateRoomDone = false;
		GCreateRoomSuccess = false;

		Protocol::C_CREATE_ROOM Pkt;
		Pkt.set_roomname(RoomName);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

		WaitForResponse(GCreateRoomDone);

		if (!GCreateRoomDone)
		{
			std::cout << "서버 응답 없음" << std::endl;
			return;
		}
		if (!GCreateRoomSuccess)
		{
			std::cout << "방 생성 실패" << std::endl;
			return;
		}

		iTargetRoomId = GCreatedRoomId;
	}
	else if (iMenuChoice == 2)
	{
		// 방 리스트 요청 (응답은 Handle_S_GET_ROOM_LIST에서 GRoomList로 채워짐)
		GRoomListDone = false;

		Protocol::C_GET_ROOM_LIST Pkt;
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

		WaitForResponse(GRoomListDone);

		if (!GRoomListDone)
		{
			std::cout << "서버 응답 없음" << std::endl;
			return;
		}

		// 방 리스트 출력
		{
			std::lock_guard<std::mutex> Lock(GRoomListMutex);
			if (GRoomList.empty())
			{
				std::cout << "존재하는 방이 없습니다." << std::endl;
				return;
			}

			std::cout << "\n=== 방 리스트 ===" << std::endl;
			for (const auto& Room : GRoomList)
			{
				std::cout << "  [" << Room.RoomId << "] " << Room.RoomName << std::endl;
			}
		}

		// 원하는 방 선택 (roomId 직접 입력)
		String SelectInput;
		std::cout << "입장할 방 ID: ";
		std::getline(std::cin, SelectInput);

		try
		{
			iTargetRoomId = std::stoi(SelectInput);
		}
		catch (const std::exception&)
		{
			std::cout << "잘못된 입력입니다." << std::endl;
			return;
		}
	}
	else if (iMenuChoice == 3)
	{
		State_ = ClientState::MyPage;
		return;
	}
	else if (iMenuChoice == 4)
	{
		State_ = ClientState::Friend;
		return;
	}
	else
	{
		std::cout << "잘못된 선택입니다." << std::endl;
		return;
	}

	// 입장 패킷 전송
	Protocol::C_ENTER_ROOM EnterPkt;
	EnterPkt.set_roomid(iTargetRoomId);
	SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(EnterPkt));

	WaitForEnter();
	State_ = ClientState::Chat;
}

// 방 입장 + 채팅 루프
void ClientApp::ChatLoop()
{
	// 진입 시 상태 초기화 + 활성화. 이후 IO 스레드의 PrintChatMessage가 프롬프트를 유지해준다.
	{
		std::lock_guard<std::mutex> Lock(GChatMutex);
		GChatMode = ChatMode::Normal;
		GChatInput.clear();
		GChatActive = true;
		std::cout << "Chat mode (Tab: 일반/확성기 토글, Enter: 전송, exit: 나가기)\n";
		RedrawPromptLocked();
	}

	while (true)
	{
		wint_t ch = _getwch();

		if (ch == L'\t')
		{
			std::lock_guard<std::mutex> Lock(GChatMutex);
			GChatMode = (GChatMode == ChatMode::Normal) ? ChatMode::Shout : ChatMode::Normal;
			RedrawPromptLocked();
			continue;
		}

		if (ch == L'\r')
		{
			String Input;
			ChatMode CurrentMode;
			{
				std::lock_guard<std::mutex> Lock(GChatMutex);
				Input = StringUtils::WideToUtf8(GChatInput);
				GChatInput.clear();
				CurrentMode = GChatMode;
				// 입력을 비우고 프롬프트만 남긴 상태로 재출력 (항상 맨 아래 한 줄 유지)
				RedrawPromptLocked();
			}

			if (Input.empty())
				continue;

			if (Input == "exit")
			{
				{
					std::lock_guard<std::mutex> Lock(GChatMutex);
					GChatActive = false;
					std::cout << "\n" << std::flush;
				}

				GExitRoomDone = false;

				Protocol::C_EXIT_ROOM ExitPkt;
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ExitPkt));

				WaitForResponse(GExitRoomDone);

				WaitForEnter();
				State_ = ClientState::Lobby;
				return;
			}

			if (CurrentMode == ChatMode::Normal)
			{
				Protocol::C_CHAT ChatPkt;
				ChatPkt.set_msg(Input);
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ChatPkt));
			}
			else
			{
				Protocol::C_SHOUT ShoutPkt;
				ShoutPkt.set_msg(Input);
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ShoutPkt));
			}

			continue;
		}

		if (ch == L'\b')
		{
			std::lock_guard<std::mutex> Lock(GChatMutex);
			if (!GChatInput.empty())
			{
				GChatInput.pop_back();
				RedrawPromptLocked();
			}
			continue;
		}

		// 그 외 제어 문자(화살표, F키 등)는 무시
		if (ch < 32)
			continue;

		{
			std::lock_guard<std::mutex> Lock(GChatMutex);
			GChatInput.push_back(static_cast<wchar_t>(ch));
			RedrawPromptLocked();
		}
	}

	// 도달 불가 (exit 분기에서 return). 방어용.
	{
		std::lock_guard<std::mutex> Lock(GChatMutex);
		GChatActive = false;
	}
	State_ = ClientState::Lobby;
}

// 마이페이지 루프: 닉네임 수정 / 계정 탈퇴 / 뒤로가기
void ClientApp::MyPageLoop()
{
	std::cout << "=== 마이페이지 ===\n";
	std::cout << "1. 닉네임 수정\n";
	std::cout << "2. 계정 탈퇴\n";
	std::cout << "3. 뒤로가기\n";
	std::cout << "> ";

	String MenuInput;
	std::getline(std::cin, MenuInput);

	int32 iMenuChoice = 0;
	try
	{
		iMenuChoice = std::stoi(MenuInput);
	}
	catch (const std::exception&)
	{
		std::cout << "잘못된 입력입니다." << std::endl;
		return;
	}

	if (iMenuChoice == 1)
	{
		// 새 닉네임 입력
		std::cout << "변경할 닉네임을 입력해주세요.\n";
		std::cout << "> ";

		String NewNickname;
		std::getline(std::cin, NewNickname);

		if (NewNickname.empty())
		{
			std::cout << "닉네임이 비어있습니다." << std::endl;
			return;
		}

		// 응답 플래그 리셋 후 송신
		GUpdateNicknameDone = false;
		GUpdateNicknameSuccess = false;

		Protocol::C_UPDATE_NICKNAME UpdatePkt;
		UpdatePkt.set_newnickname(NewNickname);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(UpdatePkt));

		WaitForResponse(GUpdateNicknameDone);

		// 결과 출력
		if (!GUpdateNicknameDone)
			std::cout << "서버 응답 없음" << std::endl;
		else if (!GUpdateNicknameSuccess)
			std::cout << "닉네임 변경 실패" << std::endl;
		else
			std::cout << "닉네임 변경 완료" << std::endl;

		WaitForEnter();
		State_ = ClientState::Lobby;
		return;
	}
	else if (iMenuChoice == 2)
	{
		// 계정 탈퇴 확인
		std::cout << "정말로 탈퇴하시겠습니까? (y/N)\n";
		std::cout << "> ";

		String Confirm;
		std::getline(std::cin, Confirm);

		if (Confirm != "y" && Confirm != "Y")
		{
			std::cout << "탈퇴 취소되었습니다." << std::endl;
			return;
		}

		// 응답 플래그 리셋 후 송신
		GDeleteAccountDone = false;
		GDeleteAccountSuccess = false;

		Protocol::C_DELETE_ACCOUNT DeletePkt;
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(DeletePkt));

		WaitForResponse(GDeleteAccountDone);

		// 결과 출력 + 분기
		if (!GDeleteAccountDone)
		{
			std::cout << "서버 응답 없음" << std::endl;
			WaitForEnter();
			return;  // MyPage 유지
		}
		if (!GDeleteAccountSuccess)
		{
			std::cout << "계정 탈퇴 실패" << std::endl;
			WaitForEnter();
			return;  // MyPage 유지
		}

		// 탈퇴 성공 -> 프로그램 종료
		std::cout << "계정 탈퇴가 완료되었습니다." << std::endl;
		WaitForEnter();
		State_ = ClientState::Exit;
		return;
	}
	else if (iMenuChoice == 3)
	{
		State_ = ClientState::Lobby;
		return;
	}
	else
	{
		std::cout << "잘못된 선택입니다." << std::endl;
	}
}

// 친구 목록 + 친구 추가/요청 확인/친구 삭제 메뉴
void ClientApp::FriendLoop()
{
	// 1. 진입 시 친구 목록 자동 fetch
	GFriendListDone = false;
	Protocol::C_GET_FRIEND_LIST GetListPkt;
	SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(GetListPkt));

	WaitForResponse(GFriendListDone);

	// 2. 목록 출력
	std::cout << "=== 친구 목록 ===\n";
	{
		std::lock_guard<std::mutex> Lock(GFriendListMutex);
		if (!GFriendListDone)
			std::cout << "(서버 응답 없음)\n";
		else if (GFriendList.empty())
			std::cout << "(친구가 없습니다)\n";
		else
		{
			for (const auto& F : GFriendList)
				std::cout << "  - " << F.Email << " (" << F.Nickname << ")\n";
		}
	}

	// 3. 메뉴
	std::cout << "\n1. 친구 추가\n";
	std::cout << "2. 친구 요청 확인\n";
	std::cout << "3. 친구 삭제\n";
	std::cout << "4. 뒤로가기\n";
	std::cout << "> ";

	String MenuInput;
	std::getline(std::cin, MenuInput);

	int32 iMenuChoice = 0;
	try
	{
		iMenuChoice = std::stoi(MenuInput);
	}
	catch (const std::exception&)
	{
		std::cout << "잘못된 입력입니다." << std::endl;
		return;
	}

	if (iMenuChoice == 1)
	{
		// 친구 추가: email 입력 -> C_REQUEST_FRIEND
		std::cout << "추가할 친구 이메일: ";
		String Email;
		std::getline(std::cin, Email);

		if (Email.empty())
		{
			std::cout << "이메일이 비어있습니다." << std::endl;
			return;
		}

		GRequestFriendDone = false;
		GRequestFriendSuccess = false;

		Protocol::C_REQUEST_FRIEND Pkt;
		Pkt.set_email(Email);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

		WaitForResponse(GRequestFriendDone);

		if (!GRequestFriendDone)
			std::cout << "서버 응답 없음" << std::endl;
		else if (GRequestFriendSuccess)
			std::cout << "친구 요청을 보냈습니다." << std::endl;

		WaitForEnter();
	}
	else if (iMenuChoice == 2)
	{
		// 받은 요청 목록 fetch
		GPendingFriendsDone = false;

		Protocol::C_GET_PENDING_FRIENDS GetPendingPkt;
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(GetPendingPkt));

		WaitForResponse(GPendingFriendsDone);

		std::cout << "\n=== 받은 친구 요청 ===\n";
		bool bEmpty = false;
		{
			std::lock_guard<std::mutex> Lock(GPendingFriendsMutex);
			if (!GPendingFriendsDone)
			{
				std::cout << "(서버 응답 없음)\n";
				WaitForEnter();
				return;
			}
			if (GPendingFriends.empty())
			{
				std::cout << "(받은 요청이 없습니다)\n";
				bEmpty = true;
			}
			else
			{
				for (const auto& F : GPendingFriends)
					std::cout << "  - " << F.Email << " (" << F.Nickname << ")\n";
			}
		}

		if (bEmpty)
		{
			WaitForEnter();
			return;
		}

		// 액션 메뉴
		std::cout << "\n1. 수락\n";
		std::cout << "2. 거절\n";
		std::cout << "3. 뒤로\n";
		std::cout << "> ";

		String ActionInput;
		std::getline(std::cin, ActionInput);

		int32 iAction = 0;
		try
		{
			iAction = std::stoi(ActionInput);
		}
		catch (const std::exception&)
		{
			std::cout << "잘못된 입력입니다." << std::endl;
			return;
		}

		if (iAction == 1 || iAction == 2)
		{
			std::cout << (iAction == 1 ? "수락할 " : "거절할 ") << "이메일: ";
			String TargetEmail;
			std::getline(std::cin, TargetEmail);

			if (TargetEmail.empty())
			{
				std::cout << "이메일이 비어있습니다." << std::endl;
				return;
			}

			if (iAction == 1)
			{
				GAcceptFriendDone = false;
				GAcceptFriendSuccess = false;

				Protocol::C_ACCEPT_FRIEND AcceptPkt;
				AcceptPkt.set_email(TargetEmail);
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(AcceptPkt));

				WaitForResponse(GAcceptFriendDone);

				if (!GAcceptFriendDone)
					std::cout << "서버 응답 없음" << std::endl;
				else if (GAcceptFriendSuccess)
					std::cout << "친구 요청을 수락했습니다." << std::endl;
			}
			else
			{
				GRejectFriendDone = false;
				GRejectFriendSuccess = false;

				Protocol::C_REJECT_FRIEND RejectPkt;
				RejectPkt.set_email(TargetEmail);
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(RejectPkt));

				WaitForResponse(GRejectFriendDone);

				if (!GRejectFriendDone)
					std::cout << "서버 응답 없음" << std::endl;
				else if (GRejectFriendSuccess)
					std::cout << "친구 요청을 거절했습니다." << std::endl;
			}

			WaitForEnter();
		}
	}
	else if (iMenuChoice == 3)
	{
		// 친구 삭제: email 입력 -> C_REMOVE_FRIEND
		std::cout << "삭제할 친구 이메일: ";
		String Email;
		std::getline(std::cin, Email);

		if (Email.empty())
		{
			std::cout << "이메일이 비어있습니다." << std::endl;
			return;
		}

		GRemoveFriendDone = false;
		GRemoveFriendSuccess = false;

		Protocol::C_REMOVE_FRIEND Pkt;
		Pkt.set_email(Email);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

		WaitForResponse(GRemoveFriendDone);

		if (!GRemoveFriendDone)
			std::cout << "서버 응답 없음" << std::endl;
		else if (GRemoveFriendSuccess)
			std::cout << "친구를 삭제했습니다." << std::endl;

		WaitForEnter();
	}
	else if (iMenuChoice == 4)
	{
		State_ = ClientState::Lobby;
		return;
	}
	else
	{
		std::cout << "잘못된 선택입니다." << std::endl;
	}
}