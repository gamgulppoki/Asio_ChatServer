#pragma once

// =============================================
// Server 레이어 전역 싱글톤 선언
// ServerCore가 아닌, 게임/앱 로직 전용 전역 객체
// =============================================

extern class RoomManager* GRoomManager;

#include "Network/ClientPacketHandler.h"
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];