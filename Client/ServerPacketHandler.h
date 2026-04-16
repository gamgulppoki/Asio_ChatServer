#pragma once
#include "Protocol.pb.h"
#include "Types.h"
#include "Packet/PacketHeader.h"
#include "Packet/SendBuffer.h"
#include "CoreGlobal.h"

class Session;

using PacketHandlerFunc = Function<bool(SharedPtr<Session>, BYTE*, int32)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

enum : uint16
{
	PKT_C_REGISTER = 1000,
	PKT_S_REGISTER = 1001,
	PKT_C_LOGIN = 1002,
	PKT_S_LOGIN = 1003,
	PKT_C_ENTER_ROOM = 1004,
	PKT_S_ENTER_ROOM = 1005,
	PKT_C_CHAT = 1006,
	PKT_S_CHAT = 1007,
};

// Forward declarations
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen);
bool Handle_S_REGISTER(SharedPtr<Session> SessionPtr, Protocol::S_REGISTER& Pkt);
bool Handle_S_LOGIN(SharedPtr<Session> SessionPtr, Protocol::S_LOGIN& Pkt);
bool Handle_S_ENTER_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_ENTER_ROOM& Pkt);
bool Handle_S_CHAT(SharedPtr<Session> SessionPtr, Protocol::S_CHAT& Pkt);

class ServerPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;

		GPacketHandler[PKT_S_REGISTER] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_REGISTER>(Handle_S_REGISTER, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_LOGIN] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_LOGIN>(Handle_S_LOGIN, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_ENTER_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_ENTER_ROOM>(Handle_S_ENTER_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_CHAT] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_CHAT>(Handle_S_CHAT, SessionPtr, Buffer, iLen);
		};
	}

	static bool HandlePacket(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
	{
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
		return GPacketHandler[Header->iId](SessionPtr, Buffer, iLen);
	}

	static SendBufferRef MakeSendBuffer(Protocol::C_REGISTER& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_REGISTER); }
	static SendBufferRef MakeSendBuffer(Protocol::C_LOGIN& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ENTER_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_ENTER_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CHAT& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_CHAT); }

private:
	template<typename PacketType, typename ProcessFunc>
	static bool HandlePacket(ProcessFunc Func, SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
	{
		PacketType Pkt;
		if (Pkt.ParseFromArray(Buffer + sizeof(PacketHeader), iLen - sizeof(PacketHeader)) == false)
			return false;

		return Func(SessionPtr, Pkt);
	}

	template<typename T>
	static SendBufferRef _MakeSendBuffer(T& Pkt, uint16 iPktId)
	{
		const uint16 iDataSize = static_cast<uint16>(Pkt.ByteSizeLong());
		const uint16 iPacketSize = iDataSize + sizeof(PacketHeader);

		SendBufferRef Buffer = GSendBufferManager->Open(iPacketSize);
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer->Data());
		Header->iSize = iPacketSize;
		Header->iId = iPktId;
		Pkt.SerializeToArray(&Header[1], iDataSize);
		Buffer->Close(iPacketSize);

		return Buffer;
	}
};