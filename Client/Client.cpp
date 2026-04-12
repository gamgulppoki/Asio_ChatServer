#include "Types.h"
#include "Session.h"
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

	// 서버로부터 메시지 수신 시 호출.
	void OnReceived(const String& Message) override
	{
		spdlog::info("Received: {}", Message);
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
		IoContext Context;
		TcpSocket Socket(Context);

		TcpResolver Resolver(Context);
		asio::connect(Socket, Resolver.resolve("127.0.0.1", std::to_string(iPort)));

		auto MySession = std::make_shared<ClientSession>(std::move(Socket));
		MySession->Start();

		// io_context를 별도 스레드에서 실행 (수신 처리용)
		std::thread IoThread([&Context]() { Context.run(); });

		// 메인 스레드에서 stdin 입력 -> 전송
		String Input;
		while (std::getline(std::cin, Input))
		{
			if (Input.empty())
				continue;

			MySession->Send(Input);
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