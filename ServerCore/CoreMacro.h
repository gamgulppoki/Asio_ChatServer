#pragma once

// =============================================
// 크래시 매크로
// =============================================

// 절대 도달해서는 안 되는 코드 경로에 사용. 도달 시 강제 크래시.
#define CRASH(cause)                        \
{                                           \
    uint32* CrashPtr = nullptr;             \
    __analysis_assume(CrashPtr != nullptr);  \
    *CrashPtr = 0xDEAD;                     \
}

// Release 빌드에서도 동작하는 assert. 조건 실패 시 크래시.
#define ASSERT_CRASH(expr)                  \
{                                           \
    if (!(expr))                            \
    {                                       \
        CRASH("ASSERT_CRASH");              \
        __analysis_assume(expr);             \
    }                                       \
}

// =============================================
// 락 매크로
// =============================================

// 클래스 멤버로 RWSpinLock을 선언
#define USE_LOCK                RWSpinLock Lock_

// 스코프 기반 Write 잠금 (RAII)
#define WRITE_LOCK              WriteLockGuard WriteLockGuard_(Lock_, __FILE__, __LINE__)

// 스코프 기반 Read 잠금 (RAII)
#define READ_LOCK               ReadLockGuard ReadLockGuard_(Lock_, __FILE__, __LINE__)