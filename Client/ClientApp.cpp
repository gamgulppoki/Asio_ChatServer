#include "ClientApp.h"
#include "CoreGlobal.h"
#include "ServerPacketHandler.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>

const int32 iPort = 9000;

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

// 서버 연결 -> 회원가입/로그인 -> 방 입장 -> 채팅
void ClientApp::Run()
{
	TcpSocket Socket(Context_);
	TcpResolver Resolver(Context_);
	asio::connect(Socket, Resolver.resolve("127.0.0.1", std::to_string(iPort)));

	SessionPtr_ = std::make_shared<ClientSession>(std::move(Socket));
	SessionPtr_->Start();

	// 수신 처리용 스레드
	std::thread IoThread([this]() { Context_.run(); });

	AuthMenu();
	ChatLoop();

	SessionPtr_->Disconnect();
	IoThread.join();
}

// 회원가입/로그인 메뉴
void ClientApp::AuthMenu()
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
			SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(Pkt));

			// 서버 응답 수신 대기
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			bLoggedIn = true;
		}
	}
}

// 방 입장 + 채팅 루프
void ClientApp::ChatLoop()
{
	// 방 입장
	std::cout << "\nEnter room number (1~3): ";
	String RoomInput;
	std::getline(std::cin, RoomInput);
	uint32 iRoomId = std::stoi(RoomInput);

	Protocol::C_ENTER_ROOM EnterPkt;
	EnterPkt.set_roomid(iRoomId);
	SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(EnterPkt));

	// 채팅 모드
	std::cout << "Chat mode (type message and press Enter):" << std::endl;
	String Input;
	while (std::getline(std::cin, Input))
	{
		if (Input.empty())
			continue;

		Protocol::C_CHAT ChatPkt;
		ChatPkt.set_msg(Input);
		SessionPtr_->Send(ServerPacketHandler::MakeSendBuffer(ChatPkt));
	}
}