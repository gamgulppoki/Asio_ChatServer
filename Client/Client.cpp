#include "ClientApp.h"
#include <spdlog/spdlog.h>
#include <windows.h>

// 클라이언트 엔트리 포인트.
int main(int argc, char* argv[])
{
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
	try
	{
		ClientApp App;
		App.Run();
	}
	catch (std::exception& Exception)
	{
		spdlog::error("Client error: {}", Exception.what());
	}

	return 0;
}