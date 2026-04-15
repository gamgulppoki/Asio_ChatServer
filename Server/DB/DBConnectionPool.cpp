#include "DBConnectionPool.h"
#include <spdlog/spdlog.h>

DBConnectionPool::DBConnectionPool()
{
}

DBConnectionPool::~DBConnectionPool()
{
	Clear();
}

// ODBC 환경 핸들을 만들고 DBConnection을 iPoolCount개 생성해 풀에 채운다.
bool DBConnectionPool::Connect(int32 iPoolCount, const WCHAR* ConnectionString)
{
	if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &Environment) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnectionPool] Failed to allocate environment handle");
		return false;
	}

	if (SQLSetEnvAttr(Environment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnectionPool] Failed to set ODBC version");
		return false;
	}

	{
		std::lock_guard<std::mutex> Lock(Mtx);

		for (int32 i = 0; i < iPoolCount; i++)
		{
			DBConnection* Conn = new DBConnection();
			if (!Conn->Connect(Environment, ConnectionString))
			{
				spdlog::error("[DBConnectionPool] Connection {} failed", i);
				delete Conn;
				return false;
			}
			Connections.push_back(Conn);
		}
	}

	spdlog::info("[DBConnectionPool] {} connections established", iPoolCount);
	return true;
}

// 모든 연결 해제 + 환경 핸들 반납.
void DBConnectionPool::Clear()
{
	std::lock_guard<std::mutex> Lock(Mtx);

	for (DBConnection* Conn : Connections)
		delete Conn;
	Connections.clear();

	if (Environment != SQL_NULL_HANDLE)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, Environment);
		Environment = SQL_NULL_HANDLE;
	}
}

// 풀이 비어있으면 Push될 때까지 블로킹 대기 후 하나 꺼낸다.
DBConnection* DBConnectionPool::Pop()
{
	std::unique_lock<std::mutex> Lock(Mtx);
	Cv.wait(Lock, [this] { return !Connections.empty(); });

	DBConnection* Conn = Connections.back();
	Connections.pop_back();
	return Conn;
}

// 반납 후 대기 중인 Pop 호출자 하나를 깨운다.
void DBConnectionPool::Push(DBConnection* Conn)
{
	{
		std::lock_guard<std::mutex> Lock(Mtx);
		Connections.push_back(Conn);
	}
	Cv.notify_one();
}

// ---------- DBConnectionScope ----------

DBConnectionScope::DBConnectionScope(DBConnectionPool* InPool)
	: Pool(InPool), Conn(InPool->Pop())
{
}

DBConnectionScope::~DBConnectionScope()
{
	if (Conn)
		Pool->Push(Conn);
}