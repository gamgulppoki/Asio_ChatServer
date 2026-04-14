#include "Types.h"
#include "Session.h"
#include "CoreGlobal.h"
#include "Packet/PacketHeader.h"
#include "Packet/SendBuffer.h"
#include "StringUtils.h"
#include "ServerPacketHandler.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>

const int32 iPort = 9000;

// 클라이언트 측 세션. 서버로부터 수신한 메시지를 출력한다.
class ClientSession : public Session
{
public:
	ClientSession(TcpSocket Socket) : Session(std::move(Socket)) {}

protected:
	// 서버 접속 완료 시 호출.
	void OnConnected() override
	{
		spdlog::info("Connected to server on port {}", iPort);
	}

	// 서버로부터 패킷 수신 시 호출. 완성된 패킷만 처리한다.
	int32 OnReceived(BYTE* Buffer, int32 iLen) override
	{
		int32 iProcessLen = 0;

		while (iLen >= sizeof(PacketHeader))
		{
			PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
			if (iLen < Header->iSize)
				break;

			ServerPacketHandler::HandlePacket(
				std::static_pointer_cast<Session>(shared_from_this()), Buffer, Header->iSize);

			Buffer += Header->iSize;
			iLen -= Header->iSize;
			iProcessLen += Header->iSize;
		}

		return iProcessLen;
	}

	// 서버 연결 종료 시 호출.
	void OnDisconnected() override
	{
		spdlog::info("Disconnected from server");
	}
};

// 테스트 클라이언트 엔트리 포인트.
int main(int argc, char* argv[])
{
	try
	{
		SendBufferManager SendBufferManagerInstance;
		GSendBufferManager = &SendBufferManagerInstance;

		IoContext Context;
		TcpSocket Socket(Context);

		TcpResolver Resolver(Context);
		asio::connect(Socket, Resolver.resolve("127.0.0.1", std::to_string(iPort)));

		ServerPacketHandler::Init();

		auto MySession = std::make_shared<ClientSession>(std::move(Socket));
		MySession->Start();

		// io_context를 별도 스레드에서 실행 (수신 처리용)
		std::thread IoThread([&Context]() { Context.run(); });

		// 방 번호 입력
		std::cout << "Enter room number (1~3): ";
		String RoomInput;
		std::getline(std::cin, RoomInput);
		uint32 iRoomId = std::stoi(RoomInput);

		Protocol::C_ENTER_ROOM EnterPkt;
		EnterPkt.set_roomid(iRoomId);
		SendBufferRef EnterBuffer = ServerPacketHandler::MakeSendBuffer(EnterPkt);
		MySession->Send(EnterBuffer);

		// 채팅 모드
		std::cout << "Chat mode (type message and press Enter):" << std::endl;
		String Input;
		while (std::getline(std::cin, Input))
		{
			if (Input.empty())
				continue;

			Protocol::C_CHAT ChatPkt;
			ChatPkt.set_msg(StringUtils::ToUtf8(Input));
			SendBufferRef Buffer = ServerPacketHandler::MakeSendBuffer(ChatPkt);
			MySession->Send(Buffer);
		}

		MySession->Disconnect();
		IoThread.join();
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Client error: {}", Exception.what());
	}

	return 0;
}