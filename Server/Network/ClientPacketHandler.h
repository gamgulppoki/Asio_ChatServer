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
	PKT_C_CREATE_ROOM = 1004,
	PKT_S_CREATE_ROOM = 1005,
	PKT_C_GET_ROOM_LIST = 1006,
	PKT_S_GET_ROOM_LIST = 1007,
	PKT_C_ENTER_ROOM = 1008,
	PKT_S_ENTER_ROOM = 1009,
	PKT_C_EXIT_ROOM = 1010,
	PKT_S_EXIT_ROOM = 1011,
	PKT_C_CHAT = 1012,
	PKT_S_CHAT = 1013,
	PKT_C_UPDATE_NICKNAME = 1014,
	PKT_S_UPDATE_NICKNAME = 1015,
	PKT_C_DELETE_ACCOUNT = 1016,
	PKT_S_DELETE_ACCOUNT = 1017,
};

// Forward declarations
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen);
bool Handle_C_REGISTER(SharedPtr<Session> SessionPtr, Protocol::C_REGISTER& Pkt);
bool Handle_C_LOGIN(SharedPtr<Session> SessionPtr, Protocol::C_LOGIN& Pkt);
bool Handle_C_CREATE_ROOM(SharedPtr<Session> SessionPtr, Protocol::C_CREATE_ROOM& Pkt);
bool Handle_C_GET_ROOM_LIST(SharedPtr<Session> SessionPtr, Protocol::C_GET_ROOM_LIST& Pkt);
bool Handle_C_ENTER_ROOM(SharedPtr<Session> SessionPtr, Protocol::C_ENTER_ROOM& Pkt);
bool Handle_C_EXIT_ROOM(SharedPtr<Session> SessionPtr, Protocol::C_EXIT_ROOM& Pkt);
bool Handle_C_CHAT(SharedPtr<Session> SessionPtr, Protocol::C_CHAT& Pkt);
bool Handle_C_UPDATE_NICKNAME(SharedPtr<Session> SessionPtr, Protocol::C_UPDATE_NICKNAME& Pkt);
bool Handle_C_DELETE_ACCOUNT(SharedPtr<Session> SessionPtr, Protocol::C_DELETE_ACCOUNT& Pkt);

class ClientPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;

		GPacketHandler[PKT_C_REGISTER] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_REGISTER>(Handle_C_REGISTER, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_LOGIN] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_CREATE_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_CREATE_ROOM>(Handle_C_CREATE_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_GET_ROOM_LIST] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_GET_ROOM_LIST>(Handle_C_GET_ROOM_LIST, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_ENTER_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_ENTER_ROOM>(Handle_C_ENTER_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_EXIT_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_EXIT_ROOM>(Handle_C_EXIT_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_CHAT] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_CHAT>(Handle_C_CHAT, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_UPDATE_NICKNAME] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_UPDATE_NICKNAME>(Handle_C_UPDATE_NICKNAME, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_C_DELETE_ACCOUNT] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::C_DELETE_ACCOUNT>(Handle_C_DELETE_ACCOUNT, SessionPtr, Buffer, iLen);
		};
	}

	static bool HandlePacket(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
	{
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
		return GPacketHandler[Header->iId](SessionPtr, Buffer, iLen);
	}

	static SendBufferRef MakeSendBuffer(Protocol::S_REGISTER& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_REGISTER); }
	static SendBufferRef MakeSendBuffer(Protocol::S_LOGIN& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_CREATE_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_CREATE_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::S_GET_ROOM_LIST& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_GET_ROOM_LIST); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_ENTER_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::S_EXIT_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_EXIT_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::S_CHAT& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_CHAT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_UPDATE_NICKNAME& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_UPDATE_NICKNAME); }
	static SendBufferRef MakeSendBuffer(Protocol::S_DELETE_ACCOUNT& Pkt) { return _MakeSendBuffer(Pkt, PKT_S_DELETE_ACCOUNT); }

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