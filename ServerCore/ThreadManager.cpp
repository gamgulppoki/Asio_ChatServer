#include "ThreadManager.h"
#include "CoreTLS.h"
#include "CoreGlobal.h"
#include "GlobalQueue.h"
#include "JobQueue.h"
#include <spdlog/spdlog.h>

static constexpr uint64 WORKER_TICK = 64;

// 생성자. 스레드 수와 종료 시그널(SIGINT, SIGTERM)을 등록한다.
ThreadManager::ThreadManager(IoContext& Context, int32 iThreadCount)
	: Context(Context)
	, iThreadCount(iThreadCount)
	, Signals(Context, SIGINT, SIGTERM)
{
	Signals.async_wait([this](const ErrorCode& Error, int iSignalNumber)
	{
		if (!Error)
		{
			spdlog::info("Signal {} received, shutting down...", iSignalNumber);
			this->Context.stop();
		}
	});
}

// 워커 스레드를 생성하고 io_context를 실행한다. 메인 스레드도 run에 참여한다.
void ThreadManager::Start()
{
	static Atomic<uint32> SThreadId(1);

	for (int32 i = 0; i < iThreadCount - 1; ++i)
	{
		Threads.emplace_back([this]()
		{
			LThreadId = SThreadId.fetch_add(1);
			DoWorkerLoop();
		});
	}

	spdlog::info("Worker threads: {}", iThreadCount);

	LThreadId = 0;
	DoWorkerLoop();
}

// I/O 처리 + GlobalQueue 처리를 번갈아 수행하는 워커 루프.
void ThreadManager::DoWorkerLoop()
{
	while (!Context.stopped())
	{
		LEndTickCount = ::GetTickCount64() + WORKER_TICK;

		// 대기 중인 I/O 이벤트 처리
		Context.poll();

		// GlobalQueue에서 JobQueue를 꺼내서 실행
		while (auto JobQueuePtr = GGlobalQueue->Pop())
		{
			JobQueuePtr->Execute();

			// 시간 초과 시 나머지는 다음 턴에
			if (::GetTickCount64() >= LEndTickCount)
				break;
		}
	}
}

// 모든 워커 스레드가 종료될 때까지 대기한다.
void ThreadManager::Join()
{
	for (auto& Thread : Threads)
	{
		if (Thread.joinable())
		{
			Thread.join();
		}
	}
}