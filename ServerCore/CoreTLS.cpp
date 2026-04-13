#include "CoreTLS.h"
#include "Packet/SendBuffer.h"
#include "JobQueue.h"

// =============================================
// TLS 변수 정의
// =============================================

thread_local uint32 LThreadId = 0;
thread_local SharedPtr<SendBufferChunk> LSendBufferChunk = nullptr;
thread_local JobQueue* LCurrentJobQueue = nullptr;
thread_local uint64 LEndTickCount = 0;