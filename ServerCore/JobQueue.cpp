#include "JobQueue.h"
#include "CoreTLS.h"
#include "CoreGlobal.h"
#include "GlobalQueue.h"

// Job을 큐에 넣는다. 상황에 따라 직접 실행하거나 GlobalQueue에 등록한다.
void JobQueue::Push(Function<void()> Callback)
{
	JobRef NewJob = std::make_shared<Job>(std::move(Callback));

	const int32 iPrevCount = iJobCount.fetch_add(1);

	{
		WRITE_LOCK;
		Jobs.push(std::move(NewJob));
	}

	// 첫 번째 Job이면 실행 시작
	if (iPrevCount == 0)
	{
		// 이 스레드가 다른 JobQueue를 실행 중이 아니면 직접 실행
		if (LCurrentJobQueue == nullptr)
			Execute();
		else
			GGlobalQueue->Push(shared_from_this());
	}
}

// 큐에 쌓인 Job들을 순차적으로 실행한다. 시간 초과 시 GlobalQueue에 재등록.
void JobQueue::Execute()
{
	LCurrentJobQueue = this;

	while (true)
	{
		Vector<JobRef> CurrentJobs;

		{
			WRITE_LOCK;
			while (Jobs.empty() == false)
			{
				CurrentJobs.push_back(std::move(Jobs.front()));
				Jobs.pop();
			}
		}

		const int32 iJobSize = static_cast<int32>(CurrentJobs.size());
		for (int32 i = 0; i < iJobSize; i++)
			CurrentJobs[i]->Execute();

		// 처리한 만큼 카운트 감소. 남은 게 없으면 종료.
		if (iJobCount.fetch_sub(iJobSize) == iJobSize)
		{
			LCurrentJobQueue = nullptr;
			return;
		}

		// 시간 초과 시 GlobalQueue에 다시 등록하고 빠져나감
		const uint64 Now = ::GetTickCount64();
		if (Now >= LEndTickCount)
		{
			LCurrentJobQueue = nullptr;
			GGlobalQueue->Push(shared_from_this());
			return;
		}
	}
}
