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
	PKT_C_REQUEST_FRIEND = 1018,
	PKT_S_REQUEST_FRIEND = 1019,
	PKT_C_ACCEPT_FRIEND = 1020,
	PKT_S_ACCEPT_FRIEND = 1021,
	PKT_C_REJECT_FRIEND = 1022,
	PKT_S_REJECT_FRIEND = 1023,
	PKT_C_GET_PENDING_FRIENDS = 1024,
	PKT_S_GET_PENDING_FRIENDS = 1025,
	PKT_C_GET_FRIEND_LIST = 1026,
	PKT_S_GET_FRIEND_LIST = 1027,
	PKT_C_REMOVE_FRIEND = 1028,
	PKT_S_REMOVE_FRIEND = 1029,
};

// Forward declarations
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen);
bool Handle_S_REGISTER(SharedPtr<Session> SessionPtr, Protocol::S_REGISTER& Pkt);
bool Handle_S_LOGIN(SharedPtr<Session> SessionPtr, Protocol::S_LOGIN& Pkt);
bool Handle_S_CREATE_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_CREATE_ROOM& Pkt);
bool Handle_S_GET_ROOM_LIST(SharedPtr<Session> SessionPtr, Protocol::S_GET_ROOM_LIST& Pkt);
bool Handle_S_ENTER_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_ENTER_ROOM& Pkt);
bool Handle_S_EXIT_ROOM(SharedPtr<Session> SessionPtr, Protocol::S_EXIT_ROOM& Pkt);
bool Handle_S_CHAT(SharedPtr<Session> SessionPtr, Protocol::S_CHAT& Pkt);
bool Handle_S_UPDATE_NICKNAME(SharedPtr<Session> SessionPtr, Protocol::S_UPDATE_NICKNAME& Pkt);
bool Handle_S_DELETE_ACCOUNT(SharedPtr<Session> SessionPtr, Protocol::S_DELETE_ACCOUNT& Pkt);
bool Handle_S_REQUEST_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REQUEST_FRIEND& Pkt);
bool Handle_S_ACCEPT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_ACCEPT_FRIEND& Pkt);
bool Handle_S_REJECT_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REJECT_FRIEND& Pkt);
bool Handle_S_GET_PENDING_FRIENDS(SharedPtr<Session> SessionPtr, Protocol::S_GET_PENDING_FRIENDS& Pkt);
bool Handle_S_GET_FRIEND_LIST(SharedPtr<Session> SessionPtr, Protocol::S_GET_FRIEND_LIST& Pkt);
bool Handle_S_REMOVE_FRIEND(SharedPtr<Session> SessionPtr, Protocol::S_REMOVE_FRIEND& Pkt);

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
		GPacketHandler[PKT_S_CREATE_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_CREATE_ROOM>(Handle_S_CREATE_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_GET_ROOM_LIST] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_GET_ROOM_LIST>(Handle_S_GET_ROOM_LIST, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_ENTER_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_ENTER_ROOM>(Handle_S_ENTER_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_EXIT_ROOM] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_EXIT_ROOM>(Handle_S_EXIT_ROOM, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_CHAT] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_CHAT>(Handle_S_CHAT, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_UPDATE_NICKNAME] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_UPDATE_NICKNAME>(Handle_S_UPDATE_NICKNAME, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_DELETE_ACCOUNT] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_DELETE_ACCOUNT>(Handle_S_DELETE_ACCOUNT, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_REQUEST_FRIEND] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_REQUEST_FRIEND>(Handle_S_REQUEST_FRIEND, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_ACCEPT_FRIEND] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_ACCEPT_FRIEND>(Handle_S_ACCEPT_FRIEND, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_REJECT_FRIEND] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_REJECT_FRIEND>(Handle_S_REJECT_FRIEND, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_GET_PENDING_FRIENDS] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_GET_PENDING_FRIENDS>(Handle_S_GET_PENDING_FRIENDS, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_GET_FRIEND_LIST] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_GET_FRIEND_LIST>(Handle_S_GET_FRIEND_LIST, SessionPtr, Buffer, iLen);
		};
		GPacketHandler[PKT_S_REMOVE_FRIEND] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::S_REMOVE_FRIEND>(Handle_S_REMOVE_FRIEND, SessionPtr, Buffer, iLen);
		};
	}

	static bool HandlePacket(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
	{
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
		return GPacketHandler[Header->iId](SessionPtr, Buffer, iLen);
	}

	static SendBufferRef MakeSendBuffer(Protocol::C_REGISTER& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_REGISTER); }
	static SendBufferRef MakeSendBuffer(Protocol::C_LOGIN& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CREATE_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_CREATE_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::C_GET_ROOM_LIST& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_GET_ROOM_LIST); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ENTER_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_ENTER_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::C_EXIT_ROOM& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_EXIT_ROOM); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CHAT& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_CHAT); }
	static SendBufferRef MakeSendBuffer(Protocol::C_UPDATE_NICKNAME& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_UPDATE_NICKNAME); }
	static SendBufferRef MakeSendBuffer(Protocol::C_DELETE_ACCOUNT& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_DELETE_ACCOUNT); }
	static SendBufferRef MakeSendBuffer(Protocol::C_REQUEST_FRIEND& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_REQUEST_FRIEND); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ACCEPT_FRIEND& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_ACCEPT_FRIEND); }
	static SendBufferRef MakeSendBuffer(Protocol::C_REJECT_FRIEND& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_REJECT_FRIEND); }
	static SendBufferRef MakeSendBuffer(Protocol::C_GET_PENDING_FRIENDS& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_GET_PENDING_FRIENDS); }
	static SendBufferRef MakeSendBuffer(Protocol::C_GET_FRIEND_LIST& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_GET_FRIEND_LIST); }
	static SendBufferRef MakeSendBuffer(Protocol::C_REMOVE_FRIEND& Pkt) { return _MakeSendBuffer(Pkt, PKT_C_REMOVE_FRIEND); }

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