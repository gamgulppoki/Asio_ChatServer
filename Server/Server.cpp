#include "ServerApp.h"
#include <spdlog/spdlog.h>
#include <windows.h>

// 서버 엔트리 포인트.
int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);
	try
	{
		ServerApp App;
		App.Run();
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Server error: {}", Exception.what());
	}

	return 0;
}