#include "Session.h"
#include <spdlog/spdlog.h>

// 세션 생성자. 소켓 소유권을 이동받는다.
Session::Session(TcpSocket Socket)
	: Socket(std::move(Socket))
{
}

// 세션을 시작한다. OnConnected를 호출하고 수신 코루틴을 생성한다.
void Session::Start()
{
	OnConnected();
	asio::co_spawn(Socket.get_executor(), DoRead(), asio::detached);
}

// 소켓에서 비동기로 데이터를 읽고, OnReceived로 전달한다.
asio::awaitable<void> Session::DoRead()
{
	auto Self = shared_from_this();
	Array<char, 1024> Buffer;

	try
	{
		while (true)
		{
			size_t iLength = co_await Socket.async_read_some(
				asio::buffer(Buffer), asio::use_awaitable);

			String Message(Buffer.data(), iLength);
			OnReceived(Message);
		}
	}
	catch (std::exception&)
	{
		OnDisconnected();
	}
}

// 쓰기 큐에 있는 메시지를 순서대로 전송한다. 큐가 비면 종료된다.
asio::awaitable<void> Session::DoWrite()
{
	auto Self = shared_from_this();

	try
	{
		while (!WriteQueue.empty())
		{
			String Message = WriteQueue.front();
			WriteQueue.pop_front();

			co_await asio::async_write(
				Socket, asio::buffer(Message), asio::use_awaitable);
		}
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Write error: {}", Exception.what());
	}

	bIsWriting = false;
}

// 메시지를 쓰기 큐에 넣고, DoWrite가 안 돌고 있으면 새로 시작한다.
void Session::Send(const String& Message)
{
	asio::post(Socket.get_executor(), [Self = shared_from_this(), Message]()
	{
		Self->WriteQueue.push_back(Message);
		if (!Self->bIsWriting)
		{
			Self->bIsWriting = true;
			asio::co_spawn(Self->Socket.get_executor(), Self->DoWrite(), asio::detached);
		}
	});
}

// 소켓을 닫는다.
void Session::Disconnect()
{
	ErrorCode Error;
	Socket.close(Error);
}