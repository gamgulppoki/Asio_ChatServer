#include "ServerGlobal.h"

#include "Network/SessionManager.h"

// =============================================
// Server 레이어 전역 싱글톤 정의
// 실제 생성은 main에서 담당, 여기선 nullptr로 초기화
// =============================================

RoomManager* GRoomManager = nullptr;
DBConnectionPool* GDBPool = nullptr;
SessionManager* GSessionManager = nullptr;

PacketHandlerFunc GPacketHandler[UINT16_MAX];