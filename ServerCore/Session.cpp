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
// 큐 접근은 WriteMutex 로 보호하고, 락은 async_write 를 기다리는 동안 잡지 않는다 (임계영역 짧게).
asio::awaitable<void> Session::DoWrite()
{
	auto Self = shared_from_this();

	try
	{
		while (true)
		{
			SendBufferRef Buffer;
			{
				std::lock_guard<std::mutex> Lock(WriteMutex);
				if (WriteQueue.empty())
				{
					// "큐가 비었다" 와 "쓰는 중 해제" 를 같은 락 안에서 처리해야
					// 그 사이에 들어온 Send 가 새 DoWrite 를 띄울지 정확히 판단할 수 있다.
					bIsWriting = false;
					co_return;
				}
				Buffer = WriteQueue.front();
				WriteQueue.pop();
			}

			co_await asio::async_write(
				Socket, asio::buffer(Buffer->Data(), Buffer->WriteSize()),
				asio::use_awaitable);
		}
	}
	catch (std::exception& Exception)
	{
		// 쓰기 실패 = 소켓이 더 이상 신뢰할 수 없는 상태. 큐를 비우고 끊어서
		// DoRead 쪽 예외 → OnDisconnected 로 정리가 흘러가게 한다.
		{
			std::lock_guard<std::mutex> Lock(WriteMutex);
			Queue<SendBufferRef>().swap(WriteQueue);
			bIsWriting = false;
		}
		spdlog::error("Write error: {}", Exception.what());
		Disconnect();
	}
}

// SendBuffer 를 쓰기 큐에 넣는다. DoWrite 가 실행 중이 아니면 새로 spawn 한다.
// 어느 스레드(핸들러, Room JobQueue 워커, AI 코루틴)에서 불려도 큐는 WriteMutex 로 보호된다.
void Session::Send(SendBufferRef Buffer)
{
	bool bStartWriter = false;
	{
		std::lock_guard<std::mutex> Lock(WriteMutex);
		WriteQueue.push(std::move(Buffer));
		if (!bIsWriting)
		{
			bIsWriting = true;   // 플래그를 락 안에서 세워야 두 Send 가 DoWrite 를 두 번 띄우지 않는다
			bStartWriter = true;
		}
	}

	if (bStartWriter)
		asio::co_spawn(Socket.get_executor(), DoWrite(), asio::detached);
}

// 소켓을 닫는다. 진행 중이던 비동기 연산들은 예외로 풀려 OnDisconnected 까지 흘러간다.
void Session::Disconnect()
{
	ErrorCode Error;
	Socket.close(Error);
}