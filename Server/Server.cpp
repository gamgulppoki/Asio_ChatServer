#include "Types.h"
#include "ThreadManager.h"
#include "Network/GameSession.h"
#include "Network/Room.h"
#include <spdlog/spdlog.h>
#include <thread>

const int32 iPort = 9000;

// 클라이언트 접속을 코루틴으로 대기하고, 접속마다 GameSession을 생성한다.
asio::awaitable<void> DoAccept(TcpAcceptor& Acceptor, Room& ChatRoom)
{
	while (true)
	{
		TcpSocket Socket = co_await Acceptor.async_accept(asio::use_awaitable);
		auto NewSession = std::make_shared<GameSession>(std::move(Socket), ChatRoom);
		NewSession->Start();
	}
}

// 서버 엔트리 포인트.
int main(int argc, char* argv[])
{
	try
	{
		IoContext Context;
		TcpAcceptor Acceptor(Context, TcpEndpoint(asio::ip::tcp::v4(), iPort));
		Room ChatRoom(Context);

		spdlog::info("Server started on port {}", iPort);

		asio::co_spawn(Context, DoAccept(Acceptor, ChatRoom), asio::detached);

		int32 iThreadCount = std::thread::hardware_concurrency();
		ThreadManager Manager(Context, iThreadCount);
		Manager.Start();
		Manager.Join();
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Server error: {}", Exception.what());
	}

	return 0;
}