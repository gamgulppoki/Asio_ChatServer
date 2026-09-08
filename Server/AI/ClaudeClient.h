#pragma once

#include "Types.h"
#include <functional>
#include <string>
#include <vector>

// Claude Messages API 스트리밍 클라이언트 (raw HTTP, 공식 C++ SDK 없음).
//
// 요청 한 번 = POST {base}/v1/messages, "stream": true → 응답은 SSE.
// 이벤트 순서: message_start(입력 토큰) → content_block_start → content_block_delta(text_delta)…
//              → content_block_stop → message_delta(stop_reason, 출력 토큰) → message_stop
// 이 중 text_delta 의 text 만 onText 로 흘리고, 토큰 수와 stop_reason 은 결과에 담는다.
namespace Claude
{
	struct Config
	{
		std::string ApiKey;                                 // ANTHROPIC_API_KEY. 비어 있으면 기능 비활성
		std::string BaseUrl   = "https://api.anthropic.com"; // ANTHROPIC_BASE_URL 로 교체 (모의 서버 테스트)
		std::string Model     = "claude-opus-5";             // CHATSERVER_AI_MODEL
		std::string Effort    = "low";                       // 채팅 용도라 지연·비용 우선 (output_config.effort)
		int32       MaxTokens = 1024;
		bool        Fallbacks = true;                        // 안전 분류기 거부 시 서버측 대체 모델 (CHATSERVER_AI_FALLBACKS=0 으로 끔)
		int32       TimeoutSeconds = 120;
	};

	struct Message
	{
		std::string Role;      // "user" | "assistant"
		std::string Content;
	};

	struct Result
	{
		bool        Success = false;
		std::string ErrorMsg;          // Success == false 일 때
		std::string StopReason;        // "end_turn" | "max_tokens" | "refusal" | ...
		int64       InputTokens  = 0;
		int64       OutputTokens = 0;
		int32       HttpStatus   = 0;
		int32       Attempts     = 0;  // 429/5xx 재시도 포함 시도 횟수
	};

	using TextFn = std::function<void(const std::string& textDelta)>;

	// 환경 변수에서 설정을 읽는다. 키가 없으면 ApiKey 가 빈 문자열.
	Config LoadConfigFromEnv();

	// 스트리밍 호출. 429 는 Retry-After 만큼(최대 5초) 기다렸다 재시도, 5xx 는 1초 뒤 재시도, 그 외 4xx 는 즉시 실패.
	// 네트워크 예외는 잡아서 Result.ErrorMsg 로 바꾼다 (호출자는 예외를 볼 일이 없다).
	asio::awaitable<Result> StreamMessage(const Config& config,
	                                      const std::string& systemPrompt,
	                                      const std::vector<Message>& messages,
	                                      TextFn onText);
}
