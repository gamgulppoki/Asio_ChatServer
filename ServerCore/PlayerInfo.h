#pragma once

#include "Types.h"

// 세션의 현재 상태
enum class SessionState
{
	Connected,   // 접속만 한 상태 (로그인 전)
	Lobby,       // 로그인 완료, 방 선택 대기
	InRoom       // 방에 입장한 상태
};

// 클라/서버 공통 유저 정보. 로그인 전에는 비어있고, 로그인 후 채워진다.
struct PlayerInfo
{
	uint64  PlayerId = 0;   // 로그인 후 User 테이블의 PK로 세팅. 로그인 전엔 0.
	WString Nickname;
};