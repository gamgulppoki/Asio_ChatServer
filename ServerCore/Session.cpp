#include "Session.h"
#include "Packet/PacketHeader.h"
#include <spdlog/spdlog.h>

// 소켓 소유권을 이동 받고 RecvBuffer를 초기화한다.
Session::Session(TcpSocket Socket)
	: Socket(std::move(Socket))
	, SessionStrand(asio::make_strand(this->Socket.get_executor()))
	, RecvBuf(4096)
{
}

// OnConnected 훅을 호출하고 수신 코루틴을 spawn 한다.
void Session::Start()
{
	OnConnected();
	asio::co_spawn(SessionStrand, DoRead(), asio::detached);
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
		// 쓰기 실패 = 소켓이 더 이상 신뢰할 수 없는 상태. 조용히 큐만 버리지 말고 끊어서
		// DoRead 쪽 예외 → OnDisconnected 로 정리가 흘러가게 한다.
		spdlog::error("Write error: {}", Exception.what());
		Disconnect();
	}

	bIsWriting = false;
}

// SendBuffer 를 쓰기 큐에 넣는다. DoWrite 가 실행 중이 아니면 새로 spawn 한다.
// 어느 스레드(핸들러, Room JobQueue 워커)에서 불려도 strand 로 post 하므로
// WriteQueue / bIsWriting 은 항상 한 스레드씩만 만진다.
void Session::Send(SendBufferRef Buffer)
{
	asio::post(SessionStrand, [Self = shared_from_this(), Buffer]()
	{
		Self->WriteQueue.push(Buffer);
		if (!Self->bIsWriting)
		{
			Self->bIsWriting = true;
			asio::co_spawn(Self->SessionStrand, Self->DoWrite(), asio::detached);
		}
	});
}

// 소켓을 닫는다. 진행 중이던 비동기 연산들은 예외로 풀려 OnDisconnected 까지 흘러간다.
void Session::Disconnect()
{
	// 소켓 close 도 strand 위에서. 이미 strand 안이면 바로, 밖(서버 종료 등)이면 post.
	if (SessionStrand.running_in_this_thread())
	{
		ErrorCode Error;
		Socket.close(Error);
		return;
	}
	asio::post(SessionStrand, [Self = shared_from_this()]()
	{
		ErrorCode Error;
		Self->Socket.close(Error);
	});
}