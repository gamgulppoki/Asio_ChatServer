#pragma once

#include <functional>
#include <string>
#include <string_view>

// Server-Sent Events 파서.
//
// SSE 는 텍스트 스트림이다. 줄 단위로 "event: 이름" / "data: 본문" 이 오고, 빈 줄이 이벤트 하나의 끝이다.
//   event: content_block_delta
//   data: {"type":"content_block_delta", ...}
//   (빈 줄)
// data: 가 여러 줄이면 '\n' 으로 이어 붙인다. ':' 로 시작하는 줄은 주석(keep-alive) 이라 버린다.
//
// HTTP 본문 조각(chunk)이 이벤트 경계와 무관하게 잘려 들어오므로, 바이트를 계속 Feed 하고
// 완성된 이벤트가 생길 때마다 콜백을 부른다.
class SseParser
{
public:
	using EventFn = std::function<void(const std::string& eventName, const std::string& data)>;

	explicit SseParser(EventFn onEvent) : OnEvent(std::move(onEvent)) {}

	void Feed(std::string_view bytes)
	{
		Pending.append(bytes.data(), bytes.size());

		size_t lineStart = 0;
		while (true)
		{
			const size_t nl = Pending.find('\n', lineStart);
			if (nl == std::string::npos)
				break;

			std::string_view line(Pending.data() + lineStart, nl - lineStart);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			HandleLine(line);
			lineStart = nl + 1;
		}
		Pending.erase(0, lineStart);
	}

	// 스트림 종료 시 남은 이벤트를 밀어낸다 (마지막 이벤트 뒤에 빈 줄이 없을 수 있음).
	void Finish()
	{
		if (!Pending.empty())
		{
			HandleLine(Pending);
			Pending.clear();
		}
		Dispatch();
	}

private:
	void HandleLine(std::string_view line)
	{
		if (line.empty())          // 빈 줄 = 이벤트 끝
		{
			Dispatch();
			return;
		}
		if (line.front() == ':')   // 주석 / keep-alive
			return;

		const size_t colon = line.find(':');
		std::string_view field = line.substr(0, colon);
		std::string_view value  = (colon == std::string_view::npos) ? std::string_view{} : line.substr(colon + 1);
		if (!value.empty() && value.front() == ' ')
			value.remove_prefix(1);

		if (field == "event")
			CurEvent.assign(value.data(), value.size());
		else if (field == "data")
		{
			if (!CurData.empty()) CurData += '\n';
			CurData.append(value.data(), value.size());
		}
		// id / retry 는 이 용도에서 쓰지 않는다
	}

	void Dispatch()
	{
		if (CurEvent.empty() && CurData.empty())
			return;
		OnEvent(CurEvent, CurData);
		CurEvent.clear();
		CurData.clear();
	}

	EventFn     OnEvent;
	std::string Pending;
	std::string CurEvent;
	std::string CurData;
};
