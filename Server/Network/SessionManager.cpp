#include "SessionManager.h"
#include "Session.h"

// 인증된 세션을 PlayerId 로 등록한다. 로그인 성공 핸들러에서 호출.
// 동일 PlayerId 가 이미 있으면 덮어씀 (중복 로그인 정책은 별도 결정 시까지 단순 overwrite).
void SessionManager::Register(uint64 playerId, SharedPtr<Session> session)
{
    std::unique_lock<std::shared_mutex> lock(Lock);
    Sessions[playerId] = session;
}

// 현재 등록된 세션이 expected 와 같을 때만 제거한다.
// 중복 로그인으로 이미 새 세션에 덮어쓰여진 상태면 no-op — 이전 세션의 disconnect 가
// 새 세션을 날리는 것을 방지한다.
void SessionManager::Unregister(uint64 playerId, SharedPtr<Session> expected)
{
    std::unique_lock<std::shared_mutex> lock(Lock);
    auto it = Sessions.find(playerId);
    if (it != Sessions.end() && it->second == expected)
        Sessions.erase(it);
}

// 모든 세션에 Disconnect() 를 호출해 소켓부터 정리하고 목록을 비운다.
// Session 소멸자가 TcpSocket 을 통해 io_context 를 참조하므로,
// 서버 종료 시 Context.reset() 보다 먼저 호출되어야 안전.
void SessionManager::Clear()
{
    std::unique_lock<std::shared_mutex> lock(Lock);
    for (auto& [SessionId, SessionPtr] : Sessions)
    {
        if (SessionPtr)
            SessionPtr->Disconnect();
    }
    Sessions.clear();
}

bool SessionManager::IsOnline(uint64 playerId)
{
    std::shared_lock lock(Lock);
    auto session = Sessions.find(playerId);
    return session != Sessions.end();
}
