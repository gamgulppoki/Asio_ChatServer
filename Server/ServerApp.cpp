#include "ServerApp.h"
#include "CoreGlobal.h"
#include "ServerGlobal.h"
#include "ThreadManager.h"
#include "Network/Listener.h"
#include "Network/ClientPacketHandler.h"
#include "DB/DBConnection.h"
#include "DB/Models/UserModel.h"
#include "DB/Models/UserCols.h"
#include "StringUtils.h"
#include <spdlog/spdlog.h>
#include <thread>

const int32 iPort = 9000;

// 전역 싱글톤 바인딩 + 패킷 핸들러 초기화
ServerApp::ServerApp()
{
	GGlobalQueue = &GlobalQueueInstance_;
	GSendBufferManager = &SendBufferManagerInstance_;
	GRoomManager = &RoomManagerInstance_;

	ClientPacketHandler::Init();
}

// 전역 싱글톤 정리 (초기화 역순)
// GThreadManager는 Run() 내 로컬 변수이므로 Run() 안에서 정리한다.
ServerApp::~ServerApp()
{
	GRoomManager = nullptr;
	GSendBufferManager = nullptr;
	GGlobalQueue = nullptr;
}

// 서버 가동. IoContext 생성 -> Listener 시작 -> 워커 스레드 가동.
void ServerApp::Run()
{
	// [테스트] DB 연결 확인
	{
		DBConnection Conn;
		bool bConnected = Conn.Connect(
			L"DRIVER={ODBC Driver 17 for SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=WebzenDB;Trusted_Connection=Yes;");

		if (bConnected)
		{
			auto UserModel = CreateUserModel(Conn);

			// SelectAll 테스트
			auto Users = UserModel.SelectAll();
			for (auto& U : Users)
			{
				spdlog::info("[ORM] User: Id={}, Name={}, Email={}",
					U.Id,
					StringUtils::WideToUtf8(U.Name),
					StringUtils::WideToUtf8(U.Email));
			}

			// Insert 테스트
			User NewUser = {};
			wcscpy_s(NewUser.Name, L"테스트유저");
			wcscpy_s(NewUser.Email, L"test@orm.com");
			UserModel.Insert(NewUser);

			// Insert 확인
			auto After = UserModel.SelectAll();
			spdlog::info("[ORM] After insert: {} users", After.size());

			// SelectOne 테스트 (Id == 1)
			auto One = UserModel.SelectOne(UserCols::Id == 1);
			if (One)
				spdlog::info("[ORM] SelectOne(Id=1): Name={}, Email={}",
					StringUtils::WideToUtf8(One->Name),
					StringUtils::WideToUtf8(One->Email));
			else
				spdlog::info("[ORM] SelectOne(Id=1): not found");

			// SelectWhere 테스트 (Name == "테스트유저")
			auto Filtered = UserModel.SelectWhere(UserCols::Name == L"테스트유저");
			spdlog::info("[ORM] SelectWhere(Name=테스트유저): {} matched", Filtered.size());

			// Update 테스트 (Name이 "테스트유저"인 행의 Email을 변경)
			User UpdateRow = {};
			wcscpy_s(UpdateRow.Name, L"테스트유저");
			wcscpy_s(UpdateRow.Email, L"updated@orm.com");
			UserModel.Update(UpdateRow, UserCols::Name == L"테스트유저");
			spdlog::info("[ORM] Update done");

			// Delete 테스트 (방금 넣은 테스트유저 정리)
			UserModel.Delete(UserCols::Name == L"테스트유저");
			spdlog::info("[ORM] After delete: {} users", UserModel.SelectAll().size());
		}
		else
		{
			spdlog::error("[ServerApp] DB connection failed");
		}
	}

	InitRooms();

	auto Context = std::make_unique<IoContext>();

	Listener AcceptListener(*Context, iPort);
	AcceptListener.Start();

	int32 iThreadCount = std::thread::hardware_concurrency();
	ThreadManager Manager(*Context, iThreadCount);
	GThreadManager = &Manager;

	Manager.Start();
	Manager.Join();

	// 명시적 종료 순서
	GThreadManager = nullptr;
	AcceptListener.Stop();
	Context.reset();
}

// 초기 방 생성
void ServerApp::InitRooms()
{
	GRoomManager->CreateRoom(L"Room 1");
	GRoomManager->CreateRoom(L"Room 2");
	GRoomManager->CreateRoom(L"Room 3");
}