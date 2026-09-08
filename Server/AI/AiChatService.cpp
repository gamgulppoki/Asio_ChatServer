#include "AiChatService.h"
#include "ClaudeClient.h"

#include "Network/GameSession.h"
#include "Network/ClientPacketHandler.h"
#include "ServerGlobal.h"
#include "DB/DBConnectionPool.h"
#include "DB/ORM/DBContext.h"
#include "DB/Entities/Entities.h"
#include "DB/Generated/EntitiesGenerated.h"
#include "StringUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>

namespace
{
	Claude::Config GConfig;
	bool  GEnabled     = false;
	int64 GDailyCalls  = 50;       // CHATSERVER_AI_DAILY_CALLS
	int64 GDailyTokens = 100000;   // CHATSERVER_AI_DAILY_TOKENS (입력+출력 합)

	constexpr int32  kHistoryRows     = 20;     // API 에 함께 보내는 최근 이력 행 수 (컨텍스트·비용 상한)
	constexpr size_t kMaxContentChars = 4000;   // AiMessage.Content NVARCHAR(4000)

	// 고정 문자열로 둔다: 요청마다 같은 프리픽스여야 API 쪽 프롬프트 캐시가 먹는다.
	const char* kSystemPrompt =
		"당신은 증권 서비스의 고객 도우미입니다. 주식, 계좌, 주문, 체결, 원장, ISA, 랩 같은 증권·금융 용어를 "
		"초보자도 이해하도록 쉽게 설명합니다. 특정 종목 추천이나 매수·매도 권유는 하지 않습니다. "
		"답은 한국어로, 간결하게 합니다.";

	int64 NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	// UTC 기준 yyyymmdd. 일 단위 한도의 키.
	int64 TodayYmd()
	{
		const std::time_t t = std::time(nullptr);
		std::tm tm{};
		gmtime_s(&tm, &t);
		return static_cast<int64>(tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
	}

	// UTF-8 경계를 지켜서 자른다 (중간에 끊긴 다바이트 문자는 DB 에 깨진 값으로 남는다).
	std::string Truncate(const std::string& s, size_t maxBytes)
	{
		if (s.size() <= maxBytes) return s;
		size_t cut = maxBytes;
		while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
		return s.substr(0, cut);
	}

	void SendDelta(const SharedPtr<GameSession>& Session, const std::string& text)
	{
		Protocol::S_AI_CHAT Pkt;
		Pkt.set_text(text);
		Pkt.set_done(false);
		Pkt.set_success(true);
		Session->Send(ClientPacketHandler::MakeSendBuffer(Pkt));
	}

	void SendDone(const SharedPtr<GameSession>& Session, bool success, const std::string& error = "")
	{
		Protocol::S_AI_CHAT Pkt;
		Pkt.set_done(true);
		Pkt.set_success(success);
		Pkt.set_error_msg(error);
		Session->Send(ClientPacketHandler::MakeSendBuffer(Pkt));
	}

	int64 EnvInt(const char* name, int64 def)
	{
		char buf[64] = {};
		size_t len = 0;
		getenv_s(&len, buf, sizeof(buf), name);
		return buf[0] ? std::max<int64>(1, std::atoll(buf)) : def;
	}

	// DB 행(오래된 순) → API 메시지. API 는 user 로 시작하고 역할이 번갈아야 한다.
	// 실패한 호출 뒤에는 user 가 연속으로 남을 수 있으므로 같은 역할은 하나로 합친다.
	std::vector<Claude::Message> BuildHistory(const std::vector<AiMessage*>& rowsOldestFirst)
	{
		std::vector<Claude::Message> out;
		for (AiMessage* row : rowsOldestFirst)
		{
			const std::string& role = row->Role.value();
			if (out.empty() && role != "user")
				continue;                                   // 맨 앞 assistant 는 버린다
			if (!out.empty() && out.back().Role == role)
			{
				out.back().Content += "\n\n" + row->Content.value();
				continue;
			}
			out.push_back({ role, row->Content.value() });
		}
		return out;
	}
}

namespace AiChatService
{
	void Init()
	{
		GConfig      = Claude::LoadConfigFromEnv();
		GEnabled     = !GConfig.ApiKey.empty();
		GDailyCalls  = EnvInt("CHATSERVER_AI_DAILY_CALLS", GDailyCalls);
		GDailyTokens = EnvInt("CHATSERVER_AI_DAILY_TOKENS", GDailyTokens);

		if (GEnabled)
			spdlog::info("[AI] enabled: model={} base={} effort={} fallbacks={} daily_calls={} daily_tokens={}",
				GConfig.Model, GConfig.BaseUrl, GConfig.Effort, GConfig.Fallbacks, GDailyCalls, GDailyTokens);
		else
			spdlog::warn("[AI] disabled: ANTHROPIC_API_KEY not set (server runs without AI chat)");
	}

	bool IsEnabled() { return GEnabled; }

	asio::awaitable<void> Run(SharedPtr<GameSession> Session, std::string UserMessage)
	{
		const int64       UserId = static_cast<int64>(Session->GetPlayerId());
		const std::string Nick   = StringUtils::WideToUtf8(Session->GetPlayerInfo().Nickname);

		if (!GEnabled)
		{
			SendDone(Session, false, "AI chat is disabled on this server (no API key)");
			co_return;
		}

		// 1. 한도 검사 + 유저 메시지 저장
		{
			DBConnectionScope Scope(GDBPool);
			DBContext ctx;
			ctx.SetDBConnection(Scope.Get());

			auto usages = ctx.Set<AiUsage>()
				.Where(Col<AiUsage>::UserId == UserId)
				.Where(Col<AiUsage>::Day == TodayYmd())
				.ToList();
			if (!usages.empty())
			{
				AiUsage* u = usages.front();
				if (u->Calls.value() >= GDailyCalls)
				{
					SendDone(Session, false, "Daily AI call limit reached (" + std::to_string(GDailyCalls) + ")");
					co_return;
				}
				if (u->InputTokens.value() + u->OutputTokens.value() >= GDailyTokens)
				{
					SendDone(Session, false, "Daily AI token limit reached (" + std::to_string(GDailyTokens) + ")");
					co_return;
				}
			}

			auto msg = std::make_unique<AiMessage>();
			msg->UserId    = UserId;
			msg->Role      = std::string("user");
			msg->Content   = Truncate(UserMessage, kMaxContentChars);
			msg->CreatedAt = NowMs();
			ctx.Set<AiMessage>().Add(std::move(msg));
			if (!ctx.SaveChanges())
			{
				SendDone(Session, false, "Database error");
				co_return;
			}
		}

		// 2. 최근 이력 로드 (방금 저장한 유저 메시지 포함). 최신 N 건을 DESC 로 뽑아 뒤집는다.
		std::vector<Claude::Message> history;
		{
			DBConnectionScope Scope(GDBPool);
			DBContext ctx;
			ctx.SetDBConnection(Scope.Get());

			auto rows = ctx.Set<AiMessage>()
				.Where(Col<AiMessage>::UserId == UserId)
				.OrderBy(Col<AiMessage>::CreatedAt, true)
				.Take(kHistoryRows)
				.ToList();
			std::reverse(rows.begin(), rows.end());
			history = BuildHistory(rows);
		}
		if (history.empty())
		{
			SendDone(Session, false, "Nothing to send");
			co_return;
		}

		// 3. 스트리밍 호출. 조각이 올 때마다 바로 클라로.
		std::string fullText;
		Claude::Result r = co_await Claude::StreamMessage(GConfig, kSystemPrompt, history,
			[&](const std::string& delta)
			{
				fullText += delta;
				SendDelta(Session, delta);
			});

		if (!r.Success)
		{
			spdlog::warn("[AI] {} failed: {} (attempts={})", Nick, r.ErrorMsg, r.Attempts);
			SendDone(Session, false, r.ErrorMsg);
			co_return;
		}

		// 안전 분류기 거부: 본문 없이 stop_reason 만 온다. 사용자에게는 정중한 한 줄.
		if (r.StopReason == "refusal" && fullText.empty())
		{
			fullText = "이 요청에는 답할 수 없습니다.";
			SendDelta(Session, fullText);
		}

		// 4. 응답 저장 + 사용량 갱신
		{
			DBConnectionScope Scope(GDBPool);
			DBContext ctx;
			ctx.SetDBConnection(Scope.Get());

			if (!fullText.empty())
			{
				auto reply = std::make_unique<AiMessage>();
				reply->UserId    = UserId;
				reply->Role      = std::string("assistant");
				reply->Content   = Truncate(fullText, kMaxContentChars);
				reply->CreatedAt = NowMs();
				ctx.Set<AiMessage>().Add(std::move(reply));
			}

			auto usages = ctx.Set<AiUsage>()
				.Where(Col<AiUsage>::UserId == UserId)
				.Where(Col<AiUsage>::Day == TodayYmd())
				.ToList();
			if (usages.empty())
			{
				auto u = std::make_unique<AiUsage>();
				u->UserId       = UserId;
				u->Day          = TodayYmd();
				u->Calls        = 1;
				u->InputTokens  = r.InputTokens;
				u->OutputTokens = r.OutputTokens;
				ctx.Set<AiUsage>().Add(std::move(u));
			}
			else
			{
				AiUsage* u = usages.front();
				u->Calls        += 1;
				u->InputTokens  += r.InputTokens;
				u->OutputTokens += r.OutputTokens;
			}

			if (!ctx.SaveChanges())
				spdlog::error("[AI] usage/history save failed for {} (occ={})", Nick, ctx.HadOCCConflict());
		}

		spdlog::info("[AI] {} ok: in={} out={} stop={} attempts={} status={}",
			Nick, r.InputTokens, r.OutputTokens, r.StopReason, r.Attempts, r.HttpStatus);
		SendDone(Session, true);
	}
}
