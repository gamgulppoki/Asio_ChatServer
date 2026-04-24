#include "CoreGlobal.h"

#include "../Server/Network/SessionManager.h"

// =============================================
// 전역 싱글톤 정의
// 실제 생성은 main에서 담당, 여기선 nullptr로 초기화
// =============================================

ThreadManager* GThreadManager = nullptr;
SendBufferManager* GSendBufferManager = nullptr;
GlobalQueue* GGlobalQueue = nullptr;