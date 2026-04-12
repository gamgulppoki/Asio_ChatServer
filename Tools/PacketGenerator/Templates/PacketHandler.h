#pragma once
#include "Protocol.pb.h"
#include "Types.h"

// PacketHeader: [2바이트 크기][2바이트 패킷 ID]
struct PacketHeader
{
	uint16 iSize;
	uint16 iId;
};

using PacketHandlerFunc = Function<bool(SharedPtr<Session>, BYTE*, int32)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

enum : uint16
{
{%- for pkt in parser.total_pkt %}
	PKT_{{ pkt.name }} = {{ pkt.id }},
{%- endfor %}
};

// Forward declarations
bool Handle_INVALID(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen);
{%- for pkt in parser.recv_pkt %}
bool Handle_{{ pkt.name }}(SharedPtr<Session> SessionPtr, Protocol::{{ pkt.name }}& Pkt);
{%- endfor %}

class {{ output }}
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
{% for pkt in parser.recv_pkt %}
		GPacketHandler[PKT_{{ pkt.name }}] = [](SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
		{
			return HandlePacket<Protocol::{{ pkt.name }}>(Handle_{{ pkt.name }}, SessionPtr, Buffer, iLen);
		};
{%- endfor %}
	}

	static bool HandlePacket(SharedPtr<Session> SessionPtr, BYTE* Buffer, int32 iLen)
	{
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer);
		return GPacketHandler[Header->iId](SessionPtr, Buffer, iLen);
	}
{% for pkt in parser.send_pkt %}
	static SendBufferRef MakeSendBuffer(Protocol::{{ pkt.name }}& Pkt) { return _MakeSendBuffer(Pkt, PKT_{{ pkt.name }}); }
{%- endfor %}

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

		SendBufferRef Buffer = std::make_shared<SendBuffer>(iPacketSize);
		PacketHeader* Header = reinterpret_cast<PacketHeader*>(Buffer->Data());
		Header->iSize = iPacketSize;
		Header->iId = iPktId;
		Pkt.SerializeToArray(&Header[1], iDataSize);
		Buffer->Close(iPacketSize);

		return Buffer;
	}
};