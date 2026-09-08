#include "ClaudeClient.h"
#include "HttpClient.h"
#include "SseParser.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>

using json = nlohmann::json;

namespace
{
	std::string GetEnv(const char* name)
	{
		char buf[512] = {};
		size_t len = 0;
		getenv_s(&len, buf, sizeof(buf), name);
		return std::string(buf);
	}

	constexpr int32 kMaxAttempts = 3;

	// 재시도 대기. 코루틴 안에서 타이머로 잔다 (워커 스레드를 재우지 않는다).
	asio::awaitable<void> Sleep(std::chrono::milliseconds ms)
	{
		asio::steady_timer timer(co_await asio::this_coro::executor);
		timer.expires_after(ms);
		co_await timer.async_wait(asio::use_awaitable);
	}

	// 오류 응답 본문 {"type":"error","error":{"type":"...","message":"..."}} 에서 메시지 추출
	std::string ExtractErrorMessage(const std::string& body, int32 status)
	{
		try
		{
			json j = json::parse(body);
			if (j.contains("error") && j["error"].contains("message"))
				return "HTTP " + std::to_string(status) + ": " + j["error"]["message"].get<std::string>();
		}
		catch (...) {}
		return "HTTP " + std::to_string(status);
	}

	// Retry-After 헤더(초). 없거나 이상하면 기본 1초, 상한 5초.
	std::chrono::milliseconds RetryAfter(const Http::ResponseHead& head)
	{
		int seconds = 1;
		if (auto it = head.Headers.find("retry-after"); it != head.Headers.end())
			seconds = std::max(0, std::atoi(it->second.c_str()));
		return std::chrono::milliseconds(std::min(seconds, 5) * 1000);
	}
}

namespace Claude
{
	Config LoadConfigFromEnv()
	{
		Config c;
		c.ApiKey = GetEnv("ANTHROPIC_API_KEY");
		if (std::string v = GetEnv("ANTHROPIC_BASE_URL"); !v.empty()) c.BaseUrl = v;
		if (std::string v = GetEnv("CHATSERVER_AI_MODEL"); !v.empty()) c.Model = v;
		if (std::string v = GetEnv("CHATSERVER_AI_EFFORT"); !v.empty()) c.Effort = v;
		if (std::string v = GetEnv("CHATSERVER_AI_FALLBACKS"); !v.empty()) c.Fallbacks = (v != "0");
		if (std::string v = GetEnv("CHATSERVER_AI_MAX_TOKENS"); !v.empty()) c.MaxTokens = std::max(1, std::atoi(v.c_str()));
		return c;
	}

	asio::awaitable<Result> StreamMessage(const Config& config,
	                                      const std::string& systemPrompt,
	                                      const std::vector<Message>& messages,
	                                      TextFn onText)
	{
		// ---- 요청 본문 (시도마다 동일) ----
		json body;
		body["model"]      = config.Model;
		body["max_tokens"] = config.MaxTokens;
		body["stream"]     = true;
		if (!systemPrompt.empty())
			body["system"] = systemPrompt;
		body["messages"] = json::array();
		for (const auto& m : messages)
			body["messages"].push_back({ {"role", m.Role}, {"content", m.Content} });
		if (!config.Effort.empty())
			body["output_config"] = { {"effort", config.Effort} };
		if (config.Fallbacks)
			body["fallbacks"] = "default";

		Http::Request req;
		if (!Http::ParseBaseUrl(config.BaseUrl, req.Scheme, req.Host, req.Port))
		{
			Result r;
			r.ErrorMsg = "invalid ANTHROPIC_BASE_URL";
			co_return r;
		}
		req.Target = "/v1/messages";
		req.Body   = body.dump();
		req.TimeoutSeconds = config.TimeoutSeconds;
		req.Headers = {
			{ "Content-Type",      "application/json" },
			{ "Accept",            "text/event-stream" },
			{ "x-api-key",         config.ApiKey },
			{ "anthropic-version", "2023-06-01" },
		};
		if (config.Fallbacks)
			req.Headers.push_back({ "anthropic-beta", "server-side-fallback-2026-07-01" });

		// ---- 재시도 루프: 429 / 5xx / 네트워크 오류만 재시도 ----
		Result result;
		for (int32 attempt = 1; attempt <= kMaxAttempts; ++attempt)
		{
			result = Result{};
			result.Attempts = attempt;

			Http::ResponseHead head;
			std::string errorBody;            // 2xx 가 아닐 때 본문을 모아 오류 메시지로
			bool streamError = false;         // SSE 안의 error 이벤트
			bool textStarted = false;         // 이미 클라에 텍스트가 나갔으면 재시도하지 않는다 (중복 출력 방지)

			SseParser sse([&](const std::string& eventName, const std::string& data)
			{
				json j;
				try { j = json::parse(data); }
				catch (...) { return; }

				const std::string type = j.value("type", eventName);
				if (type == "message_start")
				{
					if (j.contains("message") && j["message"].contains("usage"))
						result.InputTokens = j["message"]["usage"].value("input_tokens", 0);
				}
				else if (type == "content_block_delta")
				{
					const auto& delta = j["delta"];
					if (delta.value("type", "") == "text_delta")
					{
						textStarted = true;
						onText(delta.value("text", ""));
					}
					// thinking_delta 등 다른 델타는 화면에 내보내지 않는다
				}
				else if (type == "message_delta")
				{
					if (j.contains("delta"))
						result.StopReason = j["delta"].value("stop_reason", "");
					if (j.contains("usage"))
						result.OutputTokens = j["usage"].value("output_tokens", 0);
				}
				else if (type == "error")
				{
					streamError = true;
					result.ErrorMsg = j.contains("error") ? j["error"].value("message", "stream error") : "stream error";
				}
			});

			bool networkError = false;
			try
			{
				co_await Http::StreamPost(req,
					[&](const Http::ResponseHead& h) { head = h; },
					[&](std::string_view chunk)
					{
						if (head.Status >= 200 && head.Status < 300) sse.Feed(chunk);
						else                                         errorBody.append(chunk.data(), chunk.size());
					});
				sse.Finish();
			}
			catch (const std::exception& e)
			{
				// 연결 실패·타임아웃·TLS 오류. (catch 안에서는 co_await 를 못 쓰므로 플래그만 세우고 밖에서 처리)
				networkError = true;
				result.ErrorMsg = std::string("network: ") + e.what();
				spdlog::warn("[Claude] attempt {}/{} network error: {}", attempt, kMaxAttempts, e.what());
			}
			if (networkError)
			{
				// 텍스트가 이미 클라로 나갔으면 재시도해도 중복 출력이라 여기서 끝낸다.
				if (textStarted || attempt == kMaxAttempts) co_return result;
				co_await Sleep(std::chrono::seconds(1));
				continue;
			}

			result.HttpStatus = head.Status;

			if (head.Status == 429 || head.Status >= 500)
			{
				result.ErrorMsg = ExtractErrorMessage(errorBody, head.Status);
				spdlog::warn("[Claude] attempt {}/{} {} — retrying", attempt, kMaxAttempts, result.ErrorMsg);
				if (attempt == kMaxAttempts) co_return result;
				co_await Sleep(head.Status == 429 ? RetryAfter(head) : std::chrono::milliseconds(1000));
				continue;
			}

			if (head.Status < 200 || head.Status >= 300)
			{
				// 400 (잘못된 요청), 401/403 (키), 404 (모델) — 다시 보내도 같다
				result.ErrorMsg = ExtractErrorMessage(errorBody, head.Status);
				spdlog::error("[Claude] {}", result.ErrorMsg);
				co_return result;
			}

			if (streamError)
			{
				spdlog::error("[Claude] stream error: {}", result.ErrorMsg);
				co_return result;
			}

			result.Success = true;
			co_return result;
		}

		co_return result;
	}
}
