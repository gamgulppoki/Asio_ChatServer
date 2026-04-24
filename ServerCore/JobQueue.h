#pragma once

#include "Types.h"
#include "Job.h"
#include "Lock.h"

// =============================================
// JobQueue: 작업을 큐에 넣고 순차 실행하는 직렬화 장치
// 워커 스레드들이 GlobalQueue를 통해 공정하게 처리한다.
// =============================================
class JobQueue : public std::enable_shared_from_this<JobQueue>
{
public:
	void Push(Function<void()> Callback);
	void Execute();

private:
	std::mutex Mutex;
	Queue<JobRef> Jobs;
	Atomic<int32> iJobCount = 0;
};

using JobQueueRef = SharedPtr<JobQueue>;
