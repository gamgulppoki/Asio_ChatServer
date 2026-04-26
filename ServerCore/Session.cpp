#include "Session.h"
#include "Packet/PacketHeader.h"
#include <spdlog/spdlog.h>

// 소켓 소유권을 이동 받고 RecvBuffer를 초기화한다.
Session::Session(TcpSocket Socket)
	: Socket(std::move(Socket)), RecvBuf(4096)
{
}

// OnConnected 훅을 호출하고 수신 코루틴을 spawn 한다.
void Session::Start()
{
	OnConnected();
	asio::co_spawn(Socket.get_executor(), DoRead(), asio::detached);
}

// 소켓에서 비동기로 데이터를 읽고, 완성된 패킷을 OnReceived로 전달한다.
asio::awaitable<void> Session::DoRead()
{
	auto Self = shared_from_this();

	try
	{
		while (true)
		{
			RecvBuf.Clean();

			size_t iLength = co_await Socket.async_read_some(
				asio::buffer(RecvBuf.WritePos(), RecvBuf.FreeSize()),
				asio::use_awaitable);

			if (!RecvBuf.OnWrite(static_cast<int32>(iLength)))
			{
				Disconnect();
				co_return;
			}

			// 완성된 패킷을 꺼내는 루프
			int32 iProcessLen = OnReceived(RecvBuf.ReadPos(), RecvBuf.DataSize());
			if (iProcessLen < 0 || RecvBuf.DataSize() < iProcessLen)
			{
				Disconnect();
				co_return;
			}

			if (!RecvBuf.OnRead(iProcessLen))
			{
				Disconnect();
				co_return;
			}
		}
	}
	catch (std::exception&)
	{
		OnDisconnected();
	}
}

// 쓰기 큐의 SendBuffer 를 순서대로 전송한다. 큐가 비면 코루틴이 종료된다.
asio::awaitable<void> Session::DoWrite()
{
	auto Self = shared_from_this();

	try
	{
		while (!WriteQueue.empty())
		{
			SendBufferRef Buffer = WriteQueue.front();
			WriteQueue.pop();

			co_await asio::async_write(
				Socket, asio::buffer(Buffer->Data(), Buffer->WriteSize()),
				asio::use_awaitable);
		}
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Write error: {}", Exception.what());
	}

	bIsWriting = false;
}

// SendBuffer 를 쓰기 큐에 넣는다. DoWrite 가 실행 중이 아니면 새로 spawn 한다.
// asio::post 로 Socket executor 스레드에 우회시켜 WriteQueue 접근을 직렬화한다.
void Session::Send(SendBufferRef Buffer)
{
	asio::post(Socket.get_executor(), [Self = shared_from_this(), Buffer]()
	{
		Self->WriteQueue.push(Buffer);
		if (!Self->bIsWriting)
		{
			Self->bIsWriting = true;
			asio::co_spawn(Self->Socket.get_executor(), Self->DoWrite(), asio::detached);
		}
	});
}

// 소켓을 닫는다. 진행 중이던 비동기 연산들은 예외로 풀려 OnDisconnected 까지 흘러간다.
void Session::Disconnect()
{
	ErrorCode Error;
	Socket.close(Error);
}