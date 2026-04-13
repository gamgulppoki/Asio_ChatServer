#include "Lock.h"

// =============================================
// WriteLock: 아무도 읽지도 쓰지도 않을 때(0)만 진입
// =============================================
void RWSpinLock::WriteLock(const char* File, int32 Line)
{
	for (uint32 iSpinCount = 0; iSpinCount < MAX_SPIN_COUNT; iSpinCount++)
	{
		uint32 Expected = 0;

		// LockState_가 0이면 WRITE_FLAG로 교체 (성공 시 진입)
		if (LockState_.compare_exchange_strong(Expected, WRITE_FLAG))
			return;
	}

	ASSERT_CRASH(false)
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
// =============================================
void RWSpinLock::ReadLock(const char* File, int32 Line)
{
	for (uint32 iSpinCount = 0; iSpinCount < MAX_SPIN_COUNT; iSpinCount++)
	{
		// 현재 상태에서 Write Flag가 꺼져 있는 값을 기대
		uint32 Expected = LockState_.load() & READ_MASK;

		// Write 중이 아닌 상태에서 Read Count를 +1
		if (LockState_.compare_exchange_strong(Expected, Expected + 1))
			return;
	}

	ASSERT_CRASH(false)
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