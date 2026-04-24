#pragma once
#include "Types.h"
#include <shared_mutex>

class Session;

class SessionManager
{
public:
    // 인증(로그인) 완료된 세션을 PlayerId 기준으로 등록한다.
    void Register(uint64 playerId, SharedPtr<Session> session);

    // expected 포인터가 현재 맵의 세션과 동일할 때만 제거한다.
    // 중복 로그인으로 덮어쓰여진 경우 이전 세션의 disconnect 가 새 세션을 날리는 것을 방지.
    void Unregister(uint64 playerId, SharedPtr<Session> expected);

    // 모든 세션을 Disconnect 시키고 목록 비움. 서버 종료 시 io_context 파괴 전에 호출.
    void Clear();
    
    bool IsOnline(uint64 playerId);

private:
    std::shared_mutex Lock;

public:
    HashMap<uint64, SharedPtr<Session>> Sessions;
};
