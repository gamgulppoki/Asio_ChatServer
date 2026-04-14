#include "ServerApp.h"
#include "CoreGlobal.h"
#include "ServerGlobal.h"
#include "ThreadManager.h"
#include "Network/Listener.h"
#include "Network/ClientPacketHandler.h"
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
	InitRooms();

	IoContext Context;

	Listener AcceptListener(Context, iPort);
	AcceptListener.Start();

	int32 iThreadCount = std::thread::hardware_concurrency();
	ThreadManager Manager(Context, iThreadCount);
	GThreadManager = &Manager;

	Manager.Start();
	Manager.Join();

	GThreadManager = nullptr;
}

// 초기 방 생성
void ServerApp::InitRooms()
{
	GRoomManager->CreateRoom(L"Room 1");
	GRoomManager->CreateRoom(L"Room 2");
	GRoomManager->CreateRoom(L"Room 3");
}