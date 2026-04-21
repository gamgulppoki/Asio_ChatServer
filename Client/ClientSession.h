#pragma once

#include "Types.h"
#include "Session.h"
#include "Packet/PacketHeader.h"
#include "ServerPacketHandler.h"
#include <spdlog/spdlog.h>

// 클라이언트 측 세션. 서버로부터 수신한 패킷을 핸들러에 전달한다.
class ClientSession : public Session
{
public:
	ClientSession(TcpSocket Socket) : Session(std::move(Socket)) {}

protected:
	void OnConnected() override
	{
		spdlog::info("Connected to server");
	}

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

	void OnDisconnected() override
	{
		spdlog::info("Disconnected from server");
	}
	

};