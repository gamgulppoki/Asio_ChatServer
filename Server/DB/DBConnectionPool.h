#pragma once

#include "DBConnection.h"
#include <mutex>
#include <condition_variable>

// DB 연결 N개를 미리 만들어두고 Pop/Push로 대여/반납하는 풀.
// ODBC 환경 핸들(SQLHENV)은 풀이 하나만 소유하며 모든 DBConnection이 공유한다.
class DBConnectionPool
{
public:
	DBConnectionPool();
	~DBConnectionPool();

	bool Connect(int32 iPoolCount, const WCHAR* ConnectionString);
	void Clear();

	// 연결을 하나 대여한다. 풀이 비어있으면 반납될 때까지 대기(blocking).
	DBConnection* Pop();

	// 연결을 반납한다. 대기 중인 Pop 호출자 하나를 깨운다.
	void Push(DBConnection* Conn);

private:
	SQLHENV                 Environment = SQL_NULL_HANDLE;
	Vector<DBConnection*>   Connections;
	std::mutex              Mtx;
	std::condition_variable Cv;
};

// RAII 래퍼: 생성 시 Pop, 소멸 시 Push.
// 예외/early-return 상황에서도 반드시 Push가 보장된다.
class DBConnectionScope
{
public:
	DBConnectionScope(DBConnectionPool* InPool);
	~DBConnectionScope();

	DBConnection* operator->() const { return Conn; }
	DBConnection* Get() const        { return Conn; }

private:
	DBConnectionPool* Pool;
	DBConnection*     Conn;
};