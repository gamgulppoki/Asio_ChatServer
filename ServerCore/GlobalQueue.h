#pragma once

#include "Types.h"
#include "Lock.h"

class JobQueue;

// =============================================
// GlobalQueue: 실행 대기 중인 JobQueue들을 보관한다.
// 워커 스레드들이 여기서 꺼내서 Execute한다.
// =============================================
class GlobalQueue
{
public:
	void Push(SharedPtr<JobQueue> JobQueuePtr);
	SharedPtr<JobQueue> Pop();

private:
	USE_LOCK;
	Queue<SharedPtr<JobQueue>> JobQueues;
};