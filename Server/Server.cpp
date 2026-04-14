#include "ServerApp.h"
#include <spdlog/spdlog.h>

// 서버 엔트리 포인트.
int main(int argc, char* argv[])
{
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