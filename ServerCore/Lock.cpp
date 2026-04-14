#include "Lock.h"

// =============================================
// WriteLock: 아무도 읽지도 쓰지도 않을 때(0)만 진입
// 스핀 실패 시 yield로 양보하고, 타임아웃 초과 시 크래시한다.
// =============================================
void RWSpinLock::WriteLock(const char* File, int32 Line)
{
	const int64 BeginTick = ::GetTickCount64();

	while (true)
	{
		for (uint32 iSpinCount = 0; iSpinCount < MAX_SPIN_COUNT; iSpinCount++)
		{
			uint32 Expected = 0;

			if (LockState_.compare_exchange_strong(Expected, WRITE_FLAG))
				return;
		}

		if (::GetTickCount64() - BeginTick >= ACQUIRE_TIMEOUT_TICK)
			CRASH("WRITE_LOCK_TIMEOUT");

		std::this_thread::yield();
	}
}

// =============================================
// WriteUnlock: Write Flag 해제
// =============================================
void RWSpinLock::WriteUnlock()
{
	// WRITE_FLAG가 켜져 있어야 정상
	ASSERT_CRASH((LockState_.load() & WRITE_FLAG) != 0)
	LockState_.fetch_and(~WRITE_FLAG);
}

// =============================================
// ReadLock: Write 중이 아닐 때만 Read Count +1
// 스핀 실패 시 yield로 양보하고, 타임아웃 초과 시 크래시한다.
// =============================================
void RWSpinLock::ReadLock(const char* File, int32 Line)
{
	const int64 BeginTick = ::GetTickCount64();

	while (true)
	{
		for (uint32 iSpinCount = 0; iSpinCount < MAX_SPIN_COUNT; iSpinCount++)
		{
			uint32 Expected = LockState_.load() & READ_MASK;

			if (LockState_.compare_exchange_strong(Expected, Expected + 1))
				return;
		}

		if (::GetTickCount64() - BeginTick >= ACQUIRE_TIMEOUT_TICK)
			CRASH("READ_LOCK_TIMEOUT");

		std::this_thread::yield();
	}
}

// =============================================
// ReadUnlock: Read Count -1
// =============================================
void RWSpinLock::ReadUnlock()
{
	// Read Count가 0보다 커야 정상
	ASSERT_CRASH((LockState_.load() & READ_MASK) > 0)
	LockState_.fetch_sub(1);
}