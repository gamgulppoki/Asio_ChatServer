#pragma once

#include "Types.h"

// 실행할 작업 하나를 감싸는 클래스. JobQueue에 넣어서 순차 실행한다.
class Job
{
public:
	Job(const Function<void()>& Callback) : Callback(Callback) {}
	Job(Function<void()>&& Callback) : Callback(std::move(Callback)) {}

	void Execute() { Callback(); }

private:
	Function<void()> Callback;
};

using JobRef = SharedPtr<Job>;