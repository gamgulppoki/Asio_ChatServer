#pragma once

#include "Types.h"

// PacketHeader: [2바이트 크기][2바이트 패킷 ID]
struct PacketHeader
{
	uint16 iSize;
	uint16 iId;
};