#pragma once

#include "Types.h"
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// 스트리밍 HTTP/1.1 클라이언트 (독립형 asio + asio::ssl / OpenSSL).
//
// 왜 직접 짰나: 이 프로젝트는 독립형 asio 를 쓴다. Boost.Beast 는 Boost.Asio 에만 붙어서
// 쓰려면 프로젝트 전체를 Boost 로 옮겨야 한다. 필요한 건 "POST 하나 보내고 본문을 조각 단위로
// 받는 것" 뿐이라, 서버와 같은 io_context 위에서 도는 코루틴으로 그 부분만 구현했다.
//
// 범위: HTTP/1.1, Content-Length / chunked 본문, TLS (Windows 루트 인증서 저장소로 검증, SNI).
// 범위 밖: 리다이렉트, keep-alive 재사용, HTTP/2, 압축. API 엔드포인트가 고정이라 필요 없다.
namespace Http
{
	struct Request
	{
		std::string Scheme = "https";     // "https" | "http" (모의 서버 테스트용)
		std::string Host;
		std::string Port;                 // 비어 있으면 scheme 기본값
		std::string Target = "/";
		std::vector<std::pair<std::string, std::string>> Headers;
		std::string Body;
		int32 TimeoutSeconds = 120;       // 연결부터 본문 끝까지 전체 상한
	};

	struct ResponseHead
	{
		int32 Status = 0;
		std::map<std::string, std::string> Headers;   // 키는 소문자
	};

	// 헤더 파싱 직후 한 번 호출. 본문 조각이 오기 전에 상태 코드를 알 수 있다 (2xx 면 SSE, 아니면 오류 본문).
	using HeadFn  = std::function<void(const ResponseHead&)>;
	// 본문 조각 콜백. 도착하는 대로 호출된다 (SSE 스트리밍의 핵심).
	using ChunkFn = std::function<void(std::string_view)>;

	// "scheme://host[:port]" 를 분해한다. 실패 시 false.
	bool ParseBaseUrl(const std::string& url, std::string& outScheme, std::string& outHost, std::string& outPort);

	// POST 를 보내고 헤더를 onHead 로, 본문을 onChunk 로 흘린다. 본문이 끝나면 헤드를 돌려준다.
	// 네트워크 오류·타임아웃은 std::runtime_error 로 던진다 (HTTP 4xx/5xx 는 예외가 아니라 Status 로 알린다).
	asio::awaitable<ResponseHead> StreamPost(Request req, HeadFn onHead, ChunkFn onChunk);
}
