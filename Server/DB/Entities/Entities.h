#pragma once

#include "../ORM/Types.h"
#include "../ORM/Attributes.h"
#include "../ORM/Property.h"
#include "../ORM/PrimaryProperty.h"
#include "../ORM/Navigation.h"
#include <string>

namespace FriendStatus
{
    inline constexpr auto Pending  = "Pending";
    inline constexpr auto Accepted = "Accepted";
}

// 인덱스는 조회 경로에서 역산했다. 각 컬럼 옆 주석이 그 근거.
DB_ENTITY
struct User
{
    PrimaryProperty<int64>                Id;
    UNIQUE LEN(100) Property<std::string> Email;      // 로그인 · 회원가입 중복 체크
    INDEX  LEN(60)  Property<std::string> Nickname;   // 귓속말 · 친구 요청 · 이체 대상 조회
    LEN(256)        Property<std::string> Password;   // 해시 도입 시 encoded 문자열 길이 여유

    // 포인트 잔고. 이체 기능의 원장 컬럼.
    // 갱신은 반드시 DBContext 트랜잭션 + OCC(또는 UPDLOCK) 를 거친다 (Handle_C_TRANSFER 참고).
    Property<int64>                       Balance;
};


DB_ENTITY
struct Friendship
{
    PrimaryProperty<int64>        Id;
    Property<int64>               FromUserId;
    Property<int64>               ToUserId;
    LEN(16) Property<std::string> Status;

    FK(FromUserId) Navigation<User>            FromUser;
    FK(ToUserId)   Navigation<User>            ToUser;

    COMPOSITE_UNIQUE(FromUserId, ToUserId);   // 같은 방향 중복 요청 차단 + 양방향 조회
    COMPOSITE_INDEX(ToUserId, Status);        // 받은 요청 목록 (ToUserId = me AND Status = Pending)
};


// AI 채팅 대화 이력. 요청마다 최근 N 턴을 읽어 API 에 함께 보낸다 (API 는 이전 대화를 기억하지 않는다).
DB_ENTITY
struct AiMessage
{
    PrimaryProperty<int64>          Id;
    Property<int64>                 UserId;
    LEN(16)   Property<std::string> Role;        // "user" | "assistant"
    LEN(4000) Property<std::string> Content;     // NVARCHAR 상한. 넘치면 저장 시 잘라낸다
    Property<int64>                 CreatedAt;   // unix ms

    COMPOSITE_INDEX(UserId, CreatedAt);         // 최근 N 턴 조회: WHERE UserId = ? ORDER BY CreatedAt DESC
};

// AI 사용량. 유저·일 단위 호출 수와 토큰 합계. "한도 관리" 의 축소판.
DB_ENTITY
struct AiUsage
{
    PrimaryProperty<int64>          Id;
    Property<int64>                 UserId;
    Property<int64>                 Day;         // yyyymmdd (UTC)
    Property<int64>                 Calls;
    Property<int64>                 InputTokens;
    Property<int64>                 OutputTokens;

    COMPOSITE_UNIQUE(UserId, Day);              // 유저당 하루 한 행
};
