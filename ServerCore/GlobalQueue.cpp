#include "GlobalQueue.h"

// 실행 대기 중인 JobQueue를 등록한다.
void GlobalQueue::Push(SharedPtr<JobQueue> JobQueuePtr)
{
	WRITE_LOCK;
	JobQueues.push(std::move(JobQueuePtr));
}

// 대기 중인 JobQueue를 하나 꺼낸다. 없으면 nullptr.
SharedPtr<JobQueue> GlobalQueue::Pop()
{
	WRITE_LOCK;
	if (JobQueues.empty())
		return nullptr;

	SharedPtr<JobQueue> JobQueuePtr = std::move(JobQueues.front());
	JobQueues.pop();
	return JobQueuePtr;
}
