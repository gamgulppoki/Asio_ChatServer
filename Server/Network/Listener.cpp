#include "Listener.h"
#include "GameSession.h"
#include <spdlog/spdlog.h>

Listener::Listener(IoContext& Context, uint16 iPort)
	: Acceptor_(Context, TcpEndpoint(asio::ip::tcp::v4(), iPort))
{
}

// Accept 코루틴을 spawn한다.
void Listener::Start()
{
	asio::co_spawn(Acceptor_.get_executor(), DoAccept(), asio::detached);
	spdlog::info("Listening on port {}", Acceptor_.local_endpoint().port());
}

// Acceptor를 닫아 새 접속을 거부한다. DoAccept 코루틴도 종료된다.
void Listener::Stop()
{
	ErrorCode Error;
	Acceptor_.close(Error);
	spdlog::info("Listener stopped");
}

// 클라이언트 접속을 코루틴으로 대기하고, 접속마다 GameSession을 생성한다.
asio::awaitable<void> Listener::DoAccept()
{
	while (true)
	{
		TcpSocket Socket = co_await Acceptor_.async_accept(asio::use_awaitable);
		auto NewSession = std::make_shared<GameSession>(std::move(Socket));
		NewSession->Start();
	}
}