#pragma once
#include "Types.h"

// =============================================
// Read-Write SpinLock
// 상위 16비트: Write Flag, 하위 16비트: Read Count
// =============================================
class RWSpinLock
{
	static constexpr uint32 ACQUIRE_TIMEOUT_TICK = 10000;
	static constexpr uint32 MAX_SPIN_COUNT = 5000;

	// 비트 레이아웃
	static constexpr uint32 WRITE_FLAG = 0x0001'0000;
	static constexpr uint32 READ_MASK  = 0x0000'FFFF;

public:
	void WriteLock(const char* File, int32 Line);
	void WriteUnlock();
	void ReadLock(const char* File, int32 Line);
	void ReadUnlock();

private:
	Atomic<uint32> LockState_ = 0;
};

// =============================================
// RAII Guard: 스코프 끝나면 자동 해제
// =============================================
class WriteLockGuard
{
public:
	WriteLockGuard(RWSpinLock& InLock, const char* File, int32 Line)
		: Lock_(InLock)
	{
		Lock_.WriteLock(File, Line);
	}

	~WriteLockGuard()
	{
		Lock_.WriteUnlock();
	}

private:
	RWSpinLock& Lock_;
};

class ReadLockGuard
{
public:
	ReadLockGuard(RWSpinLock& InLock, const char* File, int32 Line)
		: Lock_(InLock)
	{
		Lock_.ReadLock(File, Line);
	}

	~ReadLockGuard()
	{
		Lock_.ReadUnlock();
	}

private:
	RWSpinLock& Lock_;
};