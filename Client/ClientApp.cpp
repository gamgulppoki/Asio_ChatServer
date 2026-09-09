#include "ClientApp.h"
#include "CoreGlobal.h"
#include "ConsoleUI.h"
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

// 본인 닉네임 / 현재 방 정보. ClientApp.h 선언의 정의부.
String GMyNickname;
int32  GCurrentRoomId = 0;
String GCurrentRoomName;

// 스크롤 영역 맨 아래 행이 "아직 안 끝난 AI 답변 줄" 인지. GChatMutex 보호 안에서만 읽고 쓴다.
// DrawAiLine(bFinal=false) 가 켜고, 완성 줄이 찍히면(PrintChatMessage / DrawAiLine(bFinal=true)) 꺼진다.
static bool GBottomLineIsAi = false;

namespace
{
	// ChatLoop 카톡 레이아웃 행 분배 (콘솔 높이 H 기준):
	//   1~3      : 헤더 박스 (┌─┐ / 방 정보 / └─┘)
	//   4 ~ H-3  : 채팅 스크롤 영역 (DECSTBM)
	//   H-2      : 구분선 + 안내
	//   H-1      : 입력 프롬프트
	//   H        : 여유 (스크롤 안전 마진)
	int32 ChatHeaderTopRow()    { return 1; }
	int32 ChatScrollTopRow()    { return 4; }
	int32 ChatScrollBottomRow() { return ConsoleUI::GetSize().Height - 3; }
	int32 ChatDividerRow()      { return ConsoleUI::GetSize().Height - 2; }
	int32 ChatPromptRow()       { return ConsoleUI::GetSize().Height - 1; }

	// 진입 시 화면 셋업: cls -> 헤더 박스 -> 구분선 -> 스크롤 영역 지정.
	void DrawChatLayout()
	{
		auto Sz = ConsoleUI::GetSize();
		ConsoleUI::ClearScreen();

		const int32 InnerW = Sz.Width - 2;
		const String RoomTitle = "방 #" + std::to_string(GCurrentRoomId) + " · " + GCurrentRoomName;

		// 행 1: ┌──────┐
		ConsoleUI::MoveCursor(1, 1);
		std::cout << "┌";
		for (int32 i = 0; i < InnerW; ++i) std::cout << "─";
		std::cout << "┐";

		// 행 2: │ {RoomTitle}                     │
		ConsoleUI::MoveCursor(2, 1);
		std::cout << "│ " << RoomTitle;
		const int32 PadLen = InnerW - 1 - ConsoleUI::DisplayWidth(RoomTitle);
		for (int32 i = 0; i < PadLen; ++i) std::cout << " ";
		std::cout << "│";

		// 행 3: └──────┘
		ConsoleUI::MoveCursor(3, 1);
		std::cout << "└";
		for (int32 i = 0; i < InnerW; ++i) std::cout << "─";
		std::cout << "┘";

		// 구분선 + 안내 (회색 #240)
		ConsoleUI::MoveCursor(ChatDividerRow(), 1);
		const String Hint = " Tab: 모드 / exit: 나가기 ";
		std::cout << "\033[38;5;240m";
		std::cout << "─";
		std::cout << Hint;
		const int32 DivPad = Sz.Width - 1 - ConsoleUI::DisplayWidth(Hint);
		for (int32 i = 0; i < DivPad; ++i) std::cout << "─";
		std::cout << "\033[0m";

		// 스크롤 영역 (4 ~ H-3) — 채팅 메시지가 이 안에서만 위로 흘러감
		ConsoleUI::SetScrollRegion(ChatScrollTopRow(), ChatScrollBottomRow());

		std::cout << std::flush;
	}

	// 이탈 시 정리: 스크롤 영역 해제 + cls.
	void TeardownChatLayout()
	{
		ConsoleUI::ResetScrollRegion();
		ConsoleUI::ClearScreen();
		GBottomLineIsAi = false;
	}

	// 입력 줄(H-1)에 모드 태그 + 입력 버퍼를 그린다. 반드시 GChatMutex 보호 안에서만 호출.
	void RedrawPromptLocked()
	{
		// 모드별 태그: 일반=기본색, 확성기=주황(#208), 귓속말=하늘색(#36)
		const char* Tag = "[일반]";
		switch (GChatMode)
		{
		case ChatMode::Shout:   Tag = "\033[38;5;208m[확성기]\033[0m"; break;
		case ChatMode::Whisper: Tag = "\033[36m[귓속말]\033[0m";       break;
		case ChatMode::AI:      Tag = "\033[1m[AI]\033[0m";            break;
		default: break;
		}
		ConsoleUI::MoveCursor(ChatPromptRow(), 1);
		ConsoleUI::ClearLine();
		std::cout << Tag << " > "
		          << StringUtils::WideToUtf8(GChatInput) << std::flush;
	}

	// "/대상 메시지" 를 파싱. 성공 시 true + OutTarget/OutMessage 채움.
	bool ParseWhisperInput(const String& Input, String& OutTarget, String& OutMessage)
	{
		if (Input.empty() || Input[0] != '/')
			return false;

		size_t SpaceIdx = Input.find(' ');
		if (SpaceIdx == String::npos || SpaceIdx == 1)  // 공백 없음 or 슬래시 바로 뒤 공백
			return false;

		OutTarget  = Input.substr(1, SpaceIdx - 1);
		OutMessage = Input.substr(SpaceIdx + 1);
		return !OutMessage.empty();
	}
}

// 채팅 메시지 출력. ChatLoop 중이면:
//   - 본인(bIsMine=true): 오른쪽 정렬 (콘솔 폭 - 표시폭 - 우측 마진 1)
//   - 타인(bIsMine=false): 왼쪽 들여쓰기 2칸
// 채팅 스크롤 영역 마지막 줄로 이동 -> "\n" 으로 영역 내 한 줄 스크롤 -> 메시지 출력 -> 커서 복원.
// ChatLoop 밖이면 정렬 무시하고 그냥 한 줄 출력.
void PrintChatMessage(const String& Line, bool bIsMine)
{
	std::lock_guard<std::mutex> Lock(GChatMutex);

	if (!GChatActive)
	{
		std::cout << Line << "\n" << std::flush;
		return;
	}

	String Aligned;
	if (bIsMine)
	{
		const int32 W = ConsoleUI::GetSize().Width;
		const int32 Lw = ConsoleUI::DisplayWidth(Line);
		const int32 PadLen = W - Lw - 1;
		if (PadLen > 0)
			Aligned.assign(PadLen, ' ');
		Aligned += Line;
	}
	else
	{
		Aligned = "  " + Line;
	}

	ConsoleUI::SaveCursor();
	ConsoleUI::MoveCursor(ChatScrollBottomRow(), 1);
	std::cout << "\n" << Aligned << std::flush;
	ConsoleUI::RestoreCursor();
	GBottomLineIsAi = false;   // 맨 아래 행은 이제 이 메시지. AI 부분 줄이 있었다면 위로 밀려 확정된 셈
}

// AI 답변 줄 그리기. 부분 줄은 "맨 아래 행을 지우고 지금까지 모인 텍스트로 다시 그린다" 방식이다.
// 글자마다 커서 열을 계산하지 않아도 되고(한글 2칸 폭 문제 없음), 줄바꿈·폭 계산은 완성 줄과 같은 코드를 쓴다.
bool DrawAiLine(const String& Line, bool bFinal)
{
	std::lock_guard<std::mutex> Lock(GChatMutex);

	if (!GChatActive)
		return false;

	ConsoleUI::SaveCursor();
	ConsoleUI::MoveCursor(ChatScrollBottomRow(), 1);
	if (!GBottomLineIsAi)
		std::cout << "\n";          // 스크롤 영역 안에서 한 줄 밀어 새 행 확보 (커서는 맨 아래 행에 남는다)
	ConsoleUI::ClearLine();
	std::cout << "  " << Line << std::flush;
	ConsoleUI::RestoreCursor();

	GBottomLineIsAi = !bFinal;
	return true;
}

bool IsBottomLineAi()
{
	std::lock_guard<std::mutex> Lock(GChatMutex);
	return GBottomLineIsAi;
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

// 회원가입/로그인 화면. 성공 시 Lobby 로 자동 전환.
// 직전 결과(LastMessage)는 메뉴 화면 구분선 아래에 색상 톤으로 표시.
void ClientApp::AuthLoop()
{
	bool bLoggedIn = false;
	String LastMessage;
	const char* LastMessageColor = nullptr;

	while (!bLoggedIn)
	{
		// 메뉴 화면
		ConsoleUI::ClearScreen();
		ConsoleUI::DrawHeaderBox("ChatServer", "로그인 또는 회원가입");
		std::cout << "\n";
		std::cout << "  1. 회원가입\n";
		std::cout << "  2. 로그인\n";
		std::cout << "\n";
		ConsoleUI::DrawDivider();
		if (!LastMessage.empty())
		{
			std::cout << (LastMessageColor ? LastMessageColor : "")
			          << "  " << LastMessage
			          << ConsoleUI::Color::Reset << "\n\n";
		}
		std::cout << "> " << std::flush;

		String Choice;
		std::getline(std::cin, Choice);
		LastMessage.clear();
		LastMessageColor = nullptr;

		if (Choice == "1")
		{
			// 회원가입 입력 화면
			ConsoleUI::ClearScreen();
			ConsoleUI::DrawHeaderBox("ChatServer", "회원가입");
			std::cout << "\n";

			String Name, Email, Password;
			std::cout << ConsoleUI::Color::Hint << "  닉네임   : " << ConsoleUI::Color::Reset;
			std::getline(std::cin, Name);
			std::cout << ConsoleUI::Color::Hint << "  이메일   : " << ConsoleUI::Color::Reset;
			std::getline(std::cin, Email);
			std::cout << ConsoleUI::Color::Hint << "  비밀번호 : " << ConsoleUI::Color::Reset;
			std::getline(std::cin, Password);

			GRegisterDone = false;
			GRegisterSuccess = false;

			Protocol::C_REGISTER Pkt;
			Pkt.set_name(Name);
			Pkt.set_email(Email);
			Pkt.set_password(Password);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			WaitForResponse(GRegisterDone);
			if (!GRegisterDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (GRegisterSuccess)
			{
				LastMessage = "회원가입 성공. 로그인을 진행해 주세요.";
				LastMessageColor = ConsoleUI::Color::Success;
			}
			else
			{
				LastMessage = "회원가입 실패: " + GRegisterMessage;
				LastMessageColor = ConsoleUI::Color::Error;
			}
		}
		else if (Choice == "2")
		{
			// 로그인 입력 화면
			ConsoleUI::ClearScreen();
			ConsoleUI::DrawHeaderBox("ChatServer", "로그인");
			std::cout << "\n";

			String Email, Password;
			std::cout << ConsoleUI::Color::Hint << "  이메일   : " << ConsoleUI::Color::Reset;
			std::getline(std::cin, Email);
			std::cout << ConsoleUI::Color::Hint << "  비밀번호 : " << ConsoleUI::Color::Reset;
			std::getline(std::cin, Password);

			GLoginDone = false;
			GLoginSuccess = false;

			Protocol::C_LOGIN Pkt;
			Pkt.set_email(Email);
			Pkt.set_password(Password);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			WaitForResponse(GLoginDone);
			if (!GLoginDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (GLoginSuccess)
			{
				bLoggedIn = true;
			}
			else
			{
				LastMessage = "로그인 실패: " + (GLoginMessage.empty() ? String("이메일/비밀번호를 확인해 주세요") : GLoginMessage);
				LastMessageColor = ConsoleUI::Color::Error;
			}
		}
		else
		{
			LastMessage = "잘못된 선택입니다. 1 또는 2 를 입력해 주세요.";
			LastMessageColor = ConsoleUI::Color::Error;
		}
	}

	State_ = ClientState::Lobby;
}

// 로비 화면. 메뉴 + 귓속말 토글(Tab) 을 한 화면에서 처리.
// 메뉴 선택 후 분기(방 생성/입장/마이페이지/친구). State_ 전환 시 외부 메인 루프가 다음 Loop 호출.
void ClientApp::LobbyLoop()
{
	enum class LobbyMode { Menu, Whisper };
	String LastMessage;
	const char* LastMessageColor = nullptr;

	while (State_ == ClientState::Lobby)
	{
		ConsoleUI::ClearScreen();
		ConsoleUI::DrawHeaderBox("ChatServer — 로비", GMyNickname + " 님 환영합니다");
		std::cout << "\n";
		std::cout << "  1. 방 생성\n";
		std::cout << "  2. 방 입장\n";
		std::cout << "  3. 마이페이지\n";
		std::cout << "  4. 친구 목록\n";
		std::cout << "\n";
		ConsoleUI::DrawDivider("Tab: 메뉴 / 귓속말   귓속말 형식: /닉네임 메시지");
		if (!LastMessage.empty())
		{
			std::cout << (LastMessageColor ? LastMessageColor : "")
			          << "  " << LastMessage
			          << ConsoleUI::Color::Reset << "\n\n";
			LastMessage.clear();
			LastMessageColor = nullptr;
		}

		LobbyMode Mode = LobbyMode::Menu;
		std::wstring InputBuf;

		auto RedrawLobby = [&]()
		{
			std::cout << "\r\033[2K";
			if (Mode == LobbyMode::Menu)
				std::cout << "[메뉴]   > " << StringUtils::WideToUtf8(InputBuf) << std::flush;
			else
				std::cout << ConsoleUI::Color::Info << "[귓속말]" << ConsoleUI::Color::Reset
				          << " > " << StringUtils::WideToUtf8(InputBuf) << std::flush;
		};

		RedrawLobby();

		int32 iMenuChoice = 0;
		bool  bMenuSelected = false;

		while (!bMenuSelected)
		{
			wint_t ch = _getwch();

			if (ch == L'\t')
			{
				Mode = (Mode == LobbyMode::Menu) ? LobbyMode::Whisper : LobbyMode::Menu;
				InputBuf.clear();
				RedrawLobby();
				continue;
			}

			if (ch == L'\r')
			{
				String Input = StringUtils::WideToUtf8(InputBuf);
				InputBuf.clear();
				std::cout << "\n";

				if (Mode == LobbyMode::Whisper)
				{
					String Target, Msg;
					if (Input.empty())
					{
						RedrawLobby();
						continue;
					}
					if (!ParseWhisperInput(Input, Target, Msg))
					{
						std::cout << ConsoleUI::Color::Error
						          << "  [귓속말] 형식 오류. 사용법: /대상닉네임 메시지"
						          << ConsoleUI::Color::Reset << "\n";
					}
					else
					{
						Protocol::C_WHISPER WhisperPkt;
						WhisperPkt.set_target_nickname(Target);
						WhisperPkt.set_message(Msg);
						SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(WhisperPkt));

						std::cout << ConsoleUI::Color::Info
						          << "  [귓속말 → " << Target << "] " << Msg
						          << ConsoleUI::Color::Reset << "\n";
					}
					RedrawLobby();
					continue;
				}

				try
				{
					iMenuChoice = std::stoi(Input);
					bMenuSelected = true;
				}
				catch (const std::exception&)
				{
					LastMessage = "잘못된 입력입니다. 1~4 중 선택해주세요.";
					LastMessageColor = ConsoleUI::Color::Error;
					bMenuSelected = false;
				}
				break;
			}

			if (ch == L'\b')
			{
				if (!InputBuf.empty())
				{
					InputBuf.pop_back();
					RedrawLobby();
				}
				continue;
			}

			if (ch < 32)
				continue;

			InputBuf.push_back(static_cast<wchar_t>(ch));
			RedrawLobby();
		}

		if (!bMenuSelected)
			continue;  // 다시 메뉴 그리기

		int32 iTargetRoomId = 0;

		if (iMenuChoice == 1)
		{
			// 방 생성 화면
			ConsoleUI::ClearScreen();
			ConsoleUI::DrawHeaderBox("ChatServer — 방 생성");
			std::cout << "\n";
			std::cout << ConsoleUI::Color::Hint << "  방 이름 : " << ConsoleUI::Color::Reset;

			String RoomName;
			std::getline(std::cin, RoomName);

			GCreateRoomDone = false;
			GCreateRoomSuccess = false;

			Protocol::C_CREATE_ROOM Pkt;
			Pkt.set_roomname(RoomName);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			WaitForResponse(GCreateRoomDone);

			if (!GCreateRoomDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}
			if (!GCreateRoomSuccess)
			{
				LastMessage = "방 생성 실패";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			iTargetRoomId = GCreatedRoomId;
			GCurrentRoomId = iTargetRoomId;
			GCurrentRoomName = RoomName;
		}
		else if (iMenuChoice == 2)
		{
			GRoomListDone = false;
			Protocol::C_GET_ROOM_LIST Pkt;
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			WaitForResponse(GRoomListDone);

			ConsoleUI::ClearScreen();
			ConsoleUI::DrawHeaderBox("ChatServer — 방 리스트");
			std::cout << "\n";

			if (!GRoomListDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			bool bEmpty = false;
			{
				std::lock_guard<std::mutex> Lock(GRoomListMutex);
				if (GRoomList.empty())
				{
					bEmpty = true;
				}
				else
				{
					for (const auto& Room : GRoomList)
						std::cout << "  [" << Room.RoomId << "] " << Room.RoomName << "\n";
				}
			}

			if (bEmpty)
			{
				std::cout << ConsoleUI::Color::Hint << "  (존재하는 방이 없습니다)" << ConsoleUI::Color::Reset << "\n";
				std::cout << "\n";
				ConsoleUI::DrawDivider();
				std::cout << ConsoleUI::Color::Hint << "  엔터를 눌러 돌아가기..." << ConsoleUI::Color::Reset << std::flush;
				String Dummy;
				std::getline(std::cin, Dummy);
				continue;
			}

			std::cout << "\n";
			ConsoleUI::DrawDivider();
			std::cout << ConsoleUI::Color::Hint << "  입장할 방 ID : " << ConsoleUI::Color::Reset;

			String SelectInput;
			std::getline(std::cin, SelectInput);

			try
			{
				iTargetRoomId = std::stoi(SelectInput);
			}
			catch (const std::exception&)
			{
				LastMessage = "잘못된 방 ID 입니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			GCurrentRoomId = iTargetRoomId;
			{
				std::lock_guard<std::mutex> Lock(GRoomListMutex);
				for (const auto& Room : GRoomList)
				{
					if (Room.RoomId == iTargetRoomId)
					{
						GCurrentRoomName = Room.RoomName;
						break;
					}
				}
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
			LastMessage = "잘못된 선택입니다. 1~4 중 선택해주세요.";
			LastMessageColor = ConsoleUI::Color::Error;
			continue;
		}

		// 방 입장 패킷 송신 후 ChatLoop 으로
		Protocol::C_ENTER_ROOM EnterPkt;
		EnterPkt.set_roomid(iTargetRoomId);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(EnterPkt));

		State_ = ClientState::Chat;
		return;
	}
}

// 방 입장 + 채팅 루프
void ClientApp::ChatLoop()
{
	// 진입 시 카톡 레이아웃 셋업: 헤더 박스 + 구분선 + 스크롤 영역 + 입력 프롬프트.
	{
		std::lock_guard<std::mutex> Lock(GChatMutex);
		GChatMode = ChatMode::Normal;
		GChatInput.clear();
		GChatActive = true;
		DrawChatLayout();
		RedrawPromptLocked();
	}

	while (true)
	{
		wint_t ch = _getwch();

		if (ch == L'\t')
		{
			std::lock_guard<std::mutex> Lock(GChatMutex);
			// Normal → Shout → Whisper → AI → Normal 순환
			switch (GChatMode)
			{
			case ChatMode::Normal:  GChatMode = ChatMode::Shout;   break;
			case ChatMode::Shout:   GChatMode = ChatMode::Whisper; break;
			case ChatMode::Whisper: GChatMode = ChatMode::AI;      break;
			case ChatMode::AI:      GChatMode = ChatMode::Normal;  break;
			}
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
					TeardownChatLayout();
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
			else if (CurrentMode == ChatMode::Shout)
			{
				Protocol::C_SHOUT ShoutPkt;
				ShoutPkt.set_msg(Input);
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ShoutPkt));
			}
			else if (CurrentMode == ChatMode::AI)
			{
				Protocol::C_AI_CHAT AiPkt;
				AiPkt.set_message(Input);
				SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(AiPkt));

				// 로컬 에코 — 본인 발신이라 오른쪽. 응답은 S_AI_CHAT 조각이 오는 대로 왼쪽에 흐른다.
				PrintChatMessage("\033[93m" + Input + " [나 → AI]\033[0m", true);   // 내 질문은 밝은 노란색
			}
			else // Whisper
			{
				String Target, Msg;
				if (!ParseWhisperInput(Input, Target, Msg))
				{
					// 형식 오류 — 본인 입력에 대한 시스템 경고이므로 왼쪽
					PrintChatMessage("\033[31m[귓속말] 형식 오류. 사용법: /대상닉네임 메시지\033[0m", false);
				}
				else
				{
					Protocol::C_WHISPER WhisperPkt;
					WhisperPkt.set_target_nickname(Target);
					WhisperPkt.set_message(Msg);
					SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(WhisperPkt));

					// 로컬 에코 — 본인 발신이라 오른쪽 정렬 + 화살표 → 로 방향 표시
					PrintChatMessage("\033[36m[귓속말 → " + Target + "] " + Msg + "\033[0m", true);
				}
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
// 마이페이지 화면. 닉네임 수정 / 계정 탈퇴 / 뒤로가기.
// 액션 결과는 LastMessage 로 메뉴 화면에 색상 톤으로 표시.
void ClientApp::MyPageLoop()
{
	String LastMessage;
	const char* LastMessageColor = nullptr;

	while (State_ == ClientState::MyPage)
	{
		// 진입/액션마다 잔고를 서버에서 다시 읽는다 (이체 수신으로 바뀌었을 수 있음)
		GBalanceDone = false;
		GBalanceSuccess = false;
		Protocol::C_GET_BALANCE BalancePkt;
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(BalancePkt));
		WaitForResponse(GBalanceDone);

		const String BalanceText = (GBalanceDone && GBalanceSuccess)
			? std::to_string(GMyBalance.load()) + " P"
			: "조회 실패";

		ConsoleUI::ClearScreen();
		ConsoleUI::DrawHeaderBox("마이페이지", GMyNickname + "  ·  잔고 " + BalanceText);
		std::cout << "\n";
		std::cout << "  1. 닉네임 수정\n";
		std::cout << "  2. 포인트 이체\n";
		std::cout << "  3. 계정 탈퇴\n";
		std::cout << "  4. 뒤로가기\n";
		std::cout << "\n";
		ConsoleUI::DrawDivider();
		if (!LastMessage.empty())
		{
			std::cout << (LastMessageColor ? LastMessageColor : "")
			          << "  " << LastMessage
			          << ConsoleUI::Color::Reset << "\n\n";
			LastMessage.clear();
			LastMessageColor = nullptr;
		}
		std::cout << "> " << std::flush;

		String MenuInput;
		std::getline(std::cin, MenuInput);

		int32 iMenuChoice = 0;
		try { iMenuChoice = std::stoi(MenuInput); }
		catch (const std::exception&)
		{
			LastMessage = "잘못된 입력입니다.";
			LastMessageColor = ConsoleUI::Color::Error;
			continue;
		}

		if (iMenuChoice == 1)
		{
			std::cout << "\n" << ConsoleUI::Color::Hint << "  새 닉네임 : " << ConsoleUI::Color::Reset;
			String NewNickname;
			std::getline(std::cin, NewNickname);

			if (NewNickname.empty())
			{
				LastMessage = "닉네임이 비어있습니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			GUpdateNicknameDone = false;
			GUpdateNicknameSuccess = false;

			Protocol::C_UPDATE_NICKNAME UpdatePkt;
			UpdatePkt.set_newnickname(NewNickname);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(UpdatePkt));
			WaitForResponse(GUpdateNicknameDone);

			if (!GUpdateNicknameDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (!GUpdateNicknameSuccess)
			{
				LastMessage = "닉네임 변경 실패: " + GUpdateNicknameMessage;
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else
			{
				GMyNickname = NewNickname;
				LastMessage = "닉네임이 변경되었습니다.";
				LastMessageColor = ConsoleUI::Color::Success;
			}
		}
		else if (iMenuChoice == 2)
		{
			std::cout << "\n" << ConsoleUI::Color::Hint << "  받는 사람 닉네임 : " << ConsoleUI::Color::Reset;
			String TargetNickname;
			std::getline(std::cin, TargetNickname);

			std::cout << ConsoleUI::Color::Hint << "  보낼 포인트     : " << ConsoleUI::Color::Reset;
			String AmountInput;
			std::getline(std::cin, AmountInput);

			int64 Amount = 0;
			try { Amount = std::stoll(AmountInput); }
			catch (const std::exception&)
			{
				LastMessage = "금액이 올바르지 않습니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			if (TargetNickname.empty() || Amount <= 0)
			{
				LastMessage = "닉네임과 1 이상의 금액을 입력하세요.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			GTransferDone = false;
			GTransferSuccess = false;

			Protocol::C_TRANSFER TransferPkt;
			TransferPkt.set_target_nickname(TargetNickname);
			TransferPkt.set_amount(Amount);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(TransferPkt));
			WaitForResponse(GTransferDone);

			if (!GTransferDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (!GTransferSuccess)
			{
				LastMessage = "이체 실패: " + GTransferMessage;
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else
			{
				LastMessage = TargetNickname + " 님에게 " + std::to_string(Amount) + " P 를 보냈습니다. (잔고 "
				            + std::to_string(GMyBalance.load()) + " P";
				if (GTransferRetries > 0)
					LastMessage += ", 충돌 재시도 " + std::to_string(GTransferRetries.load()) + "회";
				LastMessage += ")";
				LastMessageColor = ConsoleUI::Color::Success;
			}
		}
		else if (iMenuChoice == 3)
		{
			std::cout << "\n" << ConsoleUI::Color::Error
			          << "  정말로 탈퇴하시겠습니까? (y/N) : "
			          << ConsoleUI::Color::Reset;
			String Confirm;
			std::getline(std::cin, Confirm);

			if (Confirm != "y" && Confirm != "Y")
			{
				LastMessage = "탈퇴가 취소되었습니다.";
				LastMessageColor = ConsoleUI::Color::Hint;
				continue;
			}

			GDeleteAccountDone = false;
			GDeleteAccountSuccess = false;
			Protocol::C_DELETE_ACCOUNT DeletePkt;
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(DeletePkt));
			WaitForResponse(GDeleteAccountDone);

			if (!GDeleteAccountDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (!GDeleteAccountSuccess)
			{
				LastMessage = "계정 탈퇴 실패: " + GDeleteAccountMessage;
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else
			{
				ConsoleUI::ClearScreen();
				ConsoleUI::DrawHeaderBox("ChatServer", "계정 탈퇴가 완료되었습니다");
				std::cout << "\n";
				std::cout << ConsoleUI::Color::Hint << "  엔터를 눌러 종료..." << ConsoleUI::Color::Reset << std::flush;
				String Dummy;
				std::getline(std::cin, Dummy);
				State_ = ClientState::Exit;
				return;
			}
		}
		else if (iMenuChoice == 4)
		{
			State_ = ClientState::Lobby;
			return;
		}
		else
		{
			LastMessage = "잘못된 선택입니다. 1~4 중 선택해주세요.";
			LastMessageColor = ConsoleUI::Color::Error;
		}
	}
}

// 친구 목록 화면. 진입/액션마다 목록 자동 fetch + 메뉴 분기.
// State_ 가 Friend 가 아니게 되면(뒤로가기) 외부 메인 루프가 다음 Loop 호출.
void ClientApp::FriendLoop()
{
	String LastMessage;
	const char* LastMessageColor = nullptr;

	while (State_ == ClientState::Friend)
	{
		// 매번 친구 목록 자동 fetch (액션 후에도 최신 상태 반영)
		GFriendListDone = false;
		Protocol::C_GET_FRIEND_LIST GetListPkt;
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(GetListPkt));
		WaitForResponse(GFriendListDone);

		ConsoleUI::ClearScreen();
		ConsoleUI::DrawHeaderBox("친구 목록");
		std::cout << "\n";

		{
			std::lock_guard<std::mutex> Lock(GFriendListMutex);
			if (!GFriendListDone)
				std::cout << ConsoleUI::Color::Error << "  (서버 응답 없음)" << ConsoleUI::Color::Reset << "\n";
			else if (GFriendList.empty())
				std::cout << ConsoleUI::Color::Hint << "  (친구가 없습니다)" << ConsoleUI::Color::Reset << "\n";
			else
			{
				for (const auto& F : GFriendList)
				{
					if (F.IsOnline)
						std::cout << "  " << ConsoleUI::Color::Success << "●" << ConsoleUI::Color::Reset
						          << " " << F.Nickname
						          << ConsoleUI::Color::Hint << "  (" << F.Email << ")" << ConsoleUI::Color::Reset
						          << "\n";
					else
						std::cout << "  " << ConsoleUI::Color::Hint << "○ " << F.Nickname
						          << "  (" << F.Email << ")" << ConsoleUI::Color::Reset
						          << "\n";
				}
			}
		}

		std::cout << "\n";
		std::cout << "  1. 친구 추가\n";
		std::cout << "  2. 친구 요청 확인\n";
		std::cout << "  3. 친구 삭제\n";
		std::cout << "  4. 뒤로가기\n";
		std::cout << "\n";
		ConsoleUI::DrawDivider();
		if (!LastMessage.empty())
		{
			std::cout << (LastMessageColor ? LastMessageColor : "")
			          << "  " << LastMessage
			          << ConsoleUI::Color::Reset << "\n\n";
			LastMessage.clear();
			LastMessageColor = nullptr;
		}
		std::cout << "> " << std::flush;

		String MenuInput;
		std::getline(std::cin, MenuInput);

		int32 iMenuChoice = 0;
		try { iMenuChoice = std::stoi(MenuInput); }
		catch (const std::exception&)
		{
			LastMessage = "잘못된 입력입니다.";
			LastMessageColor = ConsoleUI::Color::Error;
			continue;
		}

		if (iMenuChoice == 1)
		{
			std::cout << "\n" << ConsoleUI::Color::Hint << "  추가할 친구 이메일 : " << ConsoleUI::Color::Reset;
			String Email;
			std::getline(std::cin, Email);

			if (Email.empty())
			{
				LastMessage = "이메일이 비어있습니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			GRequestFriendDone = false;
			GRequestFriendSuccess = false;

			Protocol::C_REQUEST_FRIEND Pkt;
			Pkt.set_email(Email);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));
			WaitForResponse(GRequestFriendDone);

			if (!GRequestFriendDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (GRequestFriendSuccess)
			{
				LastMessage = "친구 요청을 보냈습니다.";
				LastMessageColor = ConsoleUI::Color::Success;
			}
			else
			{
				LastMessage = "친구 요청 실패: " + GFriendActionMessage;
				LastMessageColor = ConsoleUI::Color::Error;
			}
		}
		else if (iMenuChoice == 2)
		{
			GPendingFriendsDone = false;
			Protocol::C_GET_PENDING_FRIENDS GetPendingPkt;
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(GetPendingPkt));
			WaitForResponse(GPendingFriendsDone);

			ConsoleUI::ClearScreen();
			ConsoleUI::DrawHeaderBox("받은 친구 요청");
			std::cout << "\n";

			bool bEmpty = false;
			{
				std::lock_guard<std::mutex> Lock(GPendingFriendsMutex);
				if (!GPendingFriendsDone)
				{
					std::cout << ConsoleUI::Color::Error << "  (서버 응답 없음)" << ConsoleUI::Color::Reset << "\n";
					bEmpty = true;
				}
				else if (GPendingFriends.empty())
				{
					std::cout << ConsoleUI::Color::Hint << "  (받은 요청이 없습니다)" << ConsoleUI::Color::Reset << "\n";
					bEmpty = true;
				}
				else
				{
					for (const auto& F : GPendingFriends)
						std::cout << "  · " << F.Nickname
						          << ConsoleUI::Color::Hint << "  (" << F.Email << ")"
						          << ConsoleUI::Color::Reset << "\n";
				}
			}

			if (bEmpty)
			{
				std::cout << "\n";
				ConsoleUI::DrawDivider();
				std::cout << ConsoleUI::Color::Hint << "  엔터를 눌러 돌아가기..." << ConsoleUI::Color::Reset << std::flush;
				String Dummy;
				std::getline(std::cin, Dummy);
				continue;
			}

			std::cout << "\n";
			std::cout << "  1. 수락\n";
			std::cout << "  2. 거절\n";
			std::cout << "  3. 뒤로\n";
			std::cout << "\n";
			ConsoleUI::DrawDivider();
			std::cout << "> " << std::flush;

			String ActionInput;
			std::getline(std::cin, ActionInput);

			int32 iAction = 0;
			try { iAction = std::stoi(ActionInput); }
			catch (const std::exception&)
			{
				LastMessage = "잘못된 입력입니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			if (iAction != 1 && iAction != 2)
				continue;  // 뒤로 또는 잘못된 번호 -> 메인 메뉴

			std::cout << "\n" << ConsoleUI::Color::Hint
			          << (iAction == 1 ? "  수락할 이메일 : " : "  거절할 이메일 : ")
			          << ConsoleUI::Color::Reset;
			String TargetEmail;
			std::getline(std::cin, TargetEmail);

			if (TargetEmail.empty())
			{
				LastMessage = "이메일이 비어있습니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
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
				{
					LastMessage = "서버 응답 없음";
					LastMessageColor = ConsoleUI::Color::Error;
				}
				else if (GAcceptFriendSuccess)
				{
					LastMessage = "친구 요청을 수락했습니다.";
					LastMessageColor = ConsoleUI::Color::Success;
				}
				else
				{
					LastMessage = "수락 실패: " + GFriendActionMessage;
					LastMessageColor = ConsoleUI::Color::Error;
				}
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
				{
					LastMessage = "서버 응답 없음";
					LastMessageColor = ConsoleUI::Color::Error;
				}
				else if (GRejectFriendSuccess)
				{
					LastMessage = "친구 요청을 거절했습니다.";
					LastMessageColor = ConsoleUI::Color::Success;
				}
				else
				{
					LastMessage = "거절 실패: " + GFriendActionMessage;
					LastMessageColor = ConsoleUI::Color::Error;
				}
			}
		}
		else if (iMenuChoice == 3)
		{
			std::cout << "\n" << ConsoleUI::Color::Hint << "  삭제할 친구 이메일 : " << ConsoleUI::Color::Reset;
			String Email;
			std::getline(std::cin, Email);

			if (Email.empty())
			{
				LastMessage = "이메일이 비어있습니다.";
				LastMessageColor = ConsoleUI::Color::Error;
				continue;
			}

			GRemoveFriendDone = false;
			GRemoveFriendSuccess = false;
			Protocol::C_REMOVE_FRIEND Pkt;
			Pkt.set_email(Email);
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));
			WaitForResponse(GRemoveFriendDone);

			if (!GRemoveFriendDone)
			{
				LastMessage = "서버 응답 없음";
				LastMessageColor = ConsoleUI::Color::Error;
			}
			else if (GRemoveFriendSuccess)
			{
				LastMessage = "친구를 삭제했습니다.";
				LastMessageColor = ConsoleUI::Color::Success;
			}
			else
			{
				LastMessage = "친구 삭제 실패: " + GFriendActionMessage;
				LastMessageColor = ConsoleUI::Color::Error;
			}
		}
		else if (iMenuChoice == 4)
		{
			State_ = ClientState::Lobby;
			return;
		}
		else
		{
			LastMessage = "잘못된 선택입니다. 1~4 중 선택해주세요.";
			LastMessageColor = ConsoleUI::Color::Error;
		}
	}
}