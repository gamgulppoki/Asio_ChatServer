#pragma once

#include "Types.h"
#include <thread>

// 워커 스레드 생명주기와 시그널 기반 Graceful Shutdown을 관리한다.
class ThreadManager
{
public:
	ThreadManager(IoContext& Context, int32 iThreadCount);

	void Start();
	void Join();

private:
	IoContext& Context;
	int32 iThreadCount;
	Vector<std::thread> Threads;
	asio::signal_set Signals;
};