#include "Types.h"
#include "ThreadManager.h"
#include "CoreGlobal.h"
#include "ServerGlobal.h"
#include "GlobalQueue.h"
#include "Packet/SendBuffer.h"
#include "Network/GameSession.h"
#include "Network/RoomManager.h"
#include "Network/ClientPacketHandler.h"
#include <spdlog/spdlog.h>
#include <thread>

const int32 iPort = 9000;

// 클라이언트 접속을 코루틴으로 대기하고, 접속마다 GameSession을 생성한다.
asio::awaitable<void> DoAccept(TcpAcceptor& Acceptor)
{
	while (true)
	{
		TcpSocket Socket = co_await Acceptor.async_accept(asio::use_awaitable);
		auto NewSession = std::make_shared<GameSession>(std::move(Socket));
		NewSession->Start();
	}
}

// 서버 엔트리 포인트.
int main(int argc, char* argv[])
{
	try
	{
		// 전역 싱글톤 초기화
		GlobalQueue GlobalQueueInstance;
		GGlobalQueue = &GlobalQueueInstance;

		SendBufferManager SendBufferManagerInstance;
		GSendBufferManager = &SendBufferManagerInstance;

		RoomManager RoomManagerInstance;
		GRoomManager = &RoomManagerInstance;

		ClientPacketHandler::Init();

		GRoomManager->CreateRoom(L"Room 1");
		GRoomManager->CreateRoom(L"Room 2");
		GRoomManager->CreateRoom(L"Room 3");

		IoContext Context;
		TcpAcceptor Acceptor(Context, TcpEndpoint(asio::ip::tcp::v4(), iPort));

		spdlog::info("Server started on port {}", iPort);

		asio::co_spawn(Context, DoAccept(Acceptor), asio::detached);

		int32 iThreadCount = std::thread::hardware_concurrency();
		ThreadManager Manager(Context, iThreadCount);
		GThreadManager = &Manager;

		Manager.Start();
		Manager.Join();

		// 전역 싱글톤 정리 (초기화 역순)
		GThreadManager = nullptr;
		GRoomManager = nullptr;
		GSendBufferManager = nullptr;
		GGlobalQueue = nullptr;
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Server error: {}", Exception.what());
	}

	return 0;
}