#include "ServerApp.h"
#include "CoreGlobal.h"
#include "ServerGlobal.h"
#include "ThreadManager.h"
#include "Network/Listener.h"
#include "Network/ClientPacketHandler.h"
#include "DB/DBConnectionPool.h"
#include "DB/ORM/Sql.h"
#include "DB/Generated/EntitiesGenerated.h"
#include <spdlog/spdlog.h>
#include <thread>

const int32 iPort = 9000;
const int32 iDBPoolCount = 10;
const WCHAR* DBConnectionString =
	L"DRIVER={ODBC Driver 17 for SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=WebzenDB;Trusted_Connection=Yes;";

// 전역 싱글톤 바인딩 + 패킷 핸들러 초기화
ServerApp::ServerApp()
{
	GGlobalQueue = &GlobalQueueInstance_;
	GSendBufferManager = &SendBufferManagerInstance_;
	GRoomManager = &RoomManagerInstance_;
	GDBPool = &DBPoolInstance_;
	GSessionManager = &SessionManagerInstance_;

	ClientPacketHandler::Init();
}

// 전역 싱글톤 정리 (초기화 역순)
// GThreadManager는 Run() 내 로컬 변수이므로 Run() 안에서 정리한다.
ServerApp::~ServerApp()
{
	GDBPool = nullptr;
	GRoomManager = nullptr;
	GSendBufferManager = nullptr;
	GGlobalQueue = nullptr;
	GSessionManager = nullptr;
}

// 서버 가동. DB 풀 초기화 -> IoContext 생성 -> Listener 시작 -> 워커 스레드 가동.
void ServerApp::Run()
{
	if (!GDBPool->Connect(iDBPoolCount, DBConnectionString))
	{
		spdlog::error("[ServerApp] DB pool initialization failed");
		return;
	}

	InitDB();
	//InitRooms();

	auto Context = std::make_unique<IoContext>();

	Listener AcceptListener(*Context, iPort);
	AcceptListener.Start();

	int32 iThreadCount = std::thread::hardware_concurrency();
	ThreadManager threadManager(*Context, iThreadCount);
	GThreadManager = &threadManager;

	threadManager.Start();
	threadManager.Join();

	// 명시적 종료 순서
	AcceptListener.Stop();           // 1. 신규 접속 차단
	GSessionManager->Clear();        // 2. 기존 세션 전부 해제 (소켓 dtor 여기서 돌게)
	GRoomManager->Clear();           // 3. Room 해제 (WeakPtr 뿐이면 별 일 없지만 명시적으로)
	GThreadManager = nullptr;        // 4. 워커 join
	Context.reset();                 // 5. io_context 파괴 (이 시점에 아무 소켓도 의존 안 함)
	GDBPool->Clear();                // 6. DB pool

}

void ServerApp::InitDB()
{
	
	// 엔티티 메타 등록 + 스키마 적용
	register_all_generated();
	{
		DBConnectionScope Scope(GDBPool);
		for (const auto& Entry : MetaRegistry::Instance().Entities)
		{
			const auto& meta = Entry.second;
			if (!Scope->Execute(create_table_sql(meta)))
			{
				spdlog::error("[ServerApp] Schema apply failed for {}", meta.TableName);
				return;
			}
		}
	}
}

// // 초기 방 생성
// void ServerApp::InitRooms()
// {
// 	GRoomManager->CreateRoom(L"Room 1");
// 	GRoomManager->CreateRoom(L"Room 2");
// 	GRoomManager->CreateRoom(L"Room 3");
// }