#pragma once

#include "Types.h"
#include "GameSession.h"
#include "ClientPacketHandler.h"
#include "../DB/DBConnectionPool.h"
#include "DB/ORM/DBContext.h"
#include "DB/Entities/Entities.h"
#include <memory>
#include <string>

// 로그인이 필요한 DB 핸들러의 공통 도입부(prelude).
//
// 친구/마이페이지/잔고 핸들러는 전부 같은 순서로 시작했다:
//   로그인 확인 → 커넥션 풀에서 연결 대여 → DBContext 연결 → (이메일로 상대 조회 | 내 행 조회)
// 이 클래스가 그 순서를 한 번만 구현한다. 핸들러는 결과만 확인하고 본론으로 간다.
//
// 실패 처리: 응답 패킷 타입이 핸들러마다 달라서 여기서는 응답을 보내지 않는다.
//   Failed() 가 true 이면 Error 에 클라에 보낼 문구가 들어 있고, 핸들러가 그걸 담아 보낸다.
//
// 연결 대여 시점: 로그인 확인을 통과한 뒤에만 풀에서 꺼낸다 (미로그인 요청이 풀을 점유하지 않게).
//   그래서 Scope 는 멤버로 바로 만들지 않고 UniquePtr 로 늦게 만든다.
//
// 수명: 이 객체가 살아 있는 동안 연결을 쥔다. 핸들러가 반환하면 소멸자가 풀에 반납한다 (DBConnectionScope RAII).
//   멤버 선언 순서 주의 — Scope 가 Db 보다 먼저 선언되어야 Db 가 먼저 소멸한다.
//   (DBContext 소멸자가 열린 트랜잭션을 Rollback 하는데, 그때 연결이 아직 살아 있어야 한다.)
class HandlerPrelude
{
public:
	explicit HandlerPrelude(const SharedPtr<GameSession>& Session);

	bool Failed() const { return Error != nullptr; }

	// 이메일로 User 한 명 조회. 없으면 nullptr 를 돌려주고 Error = "Email not found".
	User* FindUserByEmail(const std::string& Email);

	// 로그인한 본인의 User 행 조회. 없으면 nullptr 를 돌려주고 Error = "User not found".
	User* FindMe();

	int64       MyUserId = 0;
	const char* Error    = nullptr;

private:
	UniquePtr<DBConnectionScope> Scope;   // Db 보다 먼저 선언 (위 수명 설명 참고)

public:
	DBContext Db;
};

// 실패 응답 한 줄. success=false, msg 를 채워 보내고 true 를 반환한다 (핸들러의 return 값으로 쓴다).
// msg 필드가 없는 응답 패킷(S_GET_BALANCE 등)에는 컴파일되지 않는다. 그 핸들러는 직접 보낸다.
template<typename ResPktT>
bool SendFail(const SharedPtr<GameSession>& Session, ResPktT& ResPkt, const char* Msg)
{
	ResPkt.set_success(false);
	ResPkt.set_msg(Msg);
	Session->Send(ClientPacketHandler::MakeSendBuffer(ResPkt));
	return true;
}
