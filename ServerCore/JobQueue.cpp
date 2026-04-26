#include "JobQueue.h"
#include "CoreTLS.h"
#include "CoreGlobal.h"
#include "GlobalQueue.h"

// Job 을 큐에 넣는다. 첫 Job 이라면 호출 스레드가 직접 실행하거나, 다른 큐를 처리 중이면 GlobalQueue 에 등록한다.
void JobQueue::Push(Function<void()> Callback)
{
	JobRef NewJob = std::make_shared<Job>(std::move(Callback));

	const int32 iPrevCount = iJobCount.fetch_add(1);

	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Jobs.push(std::move(NewJob));
	}

	// 첫 Job 일 때만 실행 경로를 결정한다. 이후 Job 은 이미 도는 Execute 가 같이 처리한다.
	if (iPrevCount == 0)
	{
		// 호출 스레드가 다른 JobQueue 를 실행 중이 아니면 비용 흡수 차원에서 직접 실행한다.
		if (LCurrentJobQueue == nullptr)
			Execute();
		else
			GGlobalQueue->Push(shared_from_this());
	}
}

// 큐에 쌓인 Job 들을 순차 실행한다. 워커 timeslice 를 초과하면 GlobalQueue 에 재등록 후 빠져나온다.
void JobQueue::Execute()
{
	LCurrentJobQueue = this;

	while (true)
	{
		Vector<JobRef> CurrentJobs;

		{
			std::lock_guard<std::mutex> Lock(Mutex);
			while (Jobs.empty() == false)
			{
				CurrentJobs.push_back(std::move(Jobs.front()));
				Jobs.pop();
			}
		}

		const int32 iJobSize = static_cast<int32>(CurrentJobs.size());
		for (int32 i = 0; i < iJobSize; i++)
			CurrentJobs[i]->Execute();

		// 처리한 만큼 카운트를 감소시키고, 남은 Job 이 없으면 종료한다.
		if (iJobCount.fetch_sub(iJobSize) == iJobSize)
		{
			LCurrentJobQueue = nullptr;
			return;
		}

		// timeslice 초과 시 GlobalQueue 로 자기 자신을 넘기고 워커를 양보한다. (한 Room 의 워커 독점 방지)
		const uint64 Now = ::GetTickCount64();
		if (Now >= LEndTickCount)
		{
			LCurrentJobQueue = nullptr;
			GGlobalQueue->Push(shared_from_this());
			return;
		}
	}
}
