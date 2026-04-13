#pragma once
#include "Types.h"

// =============================================
// Thread Local Storage
// 스레드마다 독립적으로 갖는 변수들
// =============================================

extern thread_local uint32 LThreadId;
extern thread_local SharedPtr<class SendBufferChunk> LSendBufferChunk;
extern thread_local class JobQueue* LCurrentJobQueue;
extern thread_local uint64 LEndTickCount;