#include "ClientApp.h"
#include "CoreGlobal.h"
#include "ServerPacketHandler.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
#include <cstdlib>

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

			// 응답 대기 (최대 5초)
			for (int32 i = 0; i < 50 && !GLoginDone; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

		// 응답 대기 (최대 5초)
		for (int32 i = 0; i < 50 && !GCreateRoomDone; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

		// 응답 대기 (최대 5초)
		for (int32 i = 0; i < 50 && !GRoomListDone; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
	// 채팅 모드
	std::cout << "Chat mode (type message and press Enter):" << std::endl;
	String Input;
	while (std::getline(std::cin, Input))
	{
		// 방금 입력한 줄을 지워서 서버 브로드캐스트 "[name] msg" 포맷만 남기기
		// (ANSI: 커서 한 줄 위로 + 해당 줄 전체 지움)
		std::cout << "\033[1A\033[2K" << std::flush;

		if (Input.empty())
			continue;

		// 퇴장
		if (Input == "exit")
		{
			GExitRoomDone = false;

			Protocol::C_EXIT_ROOM ExitPkt;
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ExitPkt));

			// 응답 대기 (최대 5초)
			for (int32 i = 0; i < 50 && !GExitRoomDone; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));

			WaitForEnter();
			State_ = ClientState::Lobby;
			return;
		}

		Protocol::C_CHAT ChatPkt;
		ChatPkt.set_msg(Input);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ChatPkt));
	}

	// 채팅 루프 종료 시 로비로 복귀. 종료 조건(퇴장 명령어 등)은 세부 로직에서 추가.
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

		// 응답 대기 (최대 5초)
		for (int32 i = 0; i < 50 && !GUpdateNicknameDone; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

		// 응답 대기 (최대 5초)
		for (int32 i = 0; i < 50 && !GDeleteAccountDone; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

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