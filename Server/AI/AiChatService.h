#pragma once

#include "Types.h"
#include <string>

class GameSession;

// AI 채팅 서비스. 한 요청의 전체 흐름을 담당한다:
//   한도 검사 → 유저 메시지 저장 → 최근 이력 로드 → Claude API 스트리밍 (조각마다 클라에 push)
//   → 응답 저장 + 사용량 갱신 → 완료 패킷
//
// 실행 위치: 세션 소켓의 executor(서버 io_context) 위에서 co_spawn 된다. API 응답을 기다리는 동안
// 워커 스레드를 점유하지 않고(코루틴이 양보), DB 접근은 다른 핸들러와 같은 동기 방식이다.
// 클라로 보내는 조각은 Session::Send 가 mutex 로 송신 큐를 보호하므로 다른 핸들러의 송신과 섞여도 안전하다.
// 한 세션에 동시에 하나만 진행한다 (GameSession::AiBusy).
namespace AiChatService
{
	// 서버 시작 시 환경 변수를 읽는다. ANTHROPIC_API_KEY 가 없으면 기능이 꺼진 채로 서버는 정상 기동.
	void Init();
	bool IsEnabled();

	asio::awaitable<void> Run(SharedPtr<GameSession> Session, std::string UserMessage);
}
