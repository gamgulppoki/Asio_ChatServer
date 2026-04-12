#include "ThreadManager.h"
#include <spdlog/spdlog.h>

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
	for (int32 i = 0; i < iThreadCount - 1; ++i)
	{
		Threads.emplace_back([this]() { Context.run(); });
	}

	spdlog::info("Worker threads: {}", iThreadCount);
	Context.run();
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