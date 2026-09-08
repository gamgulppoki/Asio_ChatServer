#include "HttpClient.h"

#include <asio/ssl.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <openssl/ssl.h>
#include <openssl/x509.h>

// wincrypt.h 는 OpenSSL 과 이름이 겹치는 매크로(X509_NAME 등)를 정의한다.
// OpenSSL 헤더를 먼저 읽은 뒤에 포함하고, 겹치는 매크로는 지운다.
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS
#undef X509_CERT_PAIR
#undef PKCS7_SIGNER_INFO
#undef OCSP_REQUEST
#undef OCSP_RESPONSE

#include <algorithm>
#include <cctype>
#include <charconv>
#include <stdexcept>
#include <variant>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace asio::experimental::awaitable_operators;

namespace
{
	using SslStream = asio::ssl::stream<asio::ip::tcp::socket>;

	std::string ToLower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// Windows 루트 인증서 저장소(ROOT) 를 OpenSSL 신뢰 저장소에 넣는다.
	// OpenSSL 의 기본 경로(set_default_verify_paths)는 리눅스용이라 Windows 에선 비어 있다.
	// 검증을 끄는 대신(verify_none) 시스템이 신뢰하는 루트를 그대로 가져온다.
	void AddWindowsRootCerts(asio::ssl::context& ctx)
	{
		HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
		if (!store)
			return;

		X509_STORE* target = SSL_CTX_get_cert_store(ctx.native_handle());
		PCCERT_CONTEXT cert = nullptr;
		while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr)
		{
			const unsigned char* p = cert->pbCertEncoded;
			if (X509* x509 = d2i_X509(nullptr, &p, static_cast<long>(cert->cbCertEncoded)))
			{
				X509_STORE_add_cert(target, x509);   // 중복이면 실패하지만 무시해도 된다
				X509_free(x509);
			}
		}
		CertCloseStore(store, 0);
	}

	std::string BuildRequestText(const Http::Request& req)
	{
		std::string text;
		text += "POST " + req.Target + " HTTP/1.1\r\n";
		text += "Host: " + req.Host + "\r\n";
		text += "Connection: close\r\n";
		text += "Content-Length: " + std::to_string(req.Body.size()) + "\r\n";
		for (const auto& [k, v] : req.Headers)
			text += k + ": " + v + "\r\n";
		text += "\r\n";
		text += req.Body;
		return text;
	}

	// "HTTP/1.1 200 OK\r\nHeader: v\r\n...\r\n\r\n" → ResponseHead
	Http::ResponseHead ParseHead(std::string_view headText)
	{
		Http::ResponseHead head;
		size_t lineEnd = headText.find("\r\n");
		std::string_view statusLine = headText.substr(0, lineEnd);

		// "HTTP/1.1 200 OK"
		size_t sp1 = statusLine.find(' ');
		if (sp1 != std::string_view::npos)
		{
			std::string_view code = statusLine.substr(sp1 + 1, 3);
			std::from_chars(code.data(), code.data() + code.size(), head.Status);
		}

		size_t pos = (lineEnd == std::string_view::npos) ? headText.size() : lineEnd + 2;
		while (pos < headText.size())
		{
			size_t next = headText.find("\r\n", pos);
			if (next == std::string_view::npos) next = headText.size();
			std::string_view line = headText.substr(pos, next - pos);
			pos = next + 2;
			if (line.empty()) break;

			size_t colon = line.find(':');
			if (colon == std::string_view::npos) continue;
			std::string key = ToLower(std::string(line.substr(0, colon)));
			std::string_view value = line.substr(colon + 1);
			while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
			head.Headers[key] = std::string(value);
		}
		return head;
	}

	// 스트림 종류(TLS / 평문)에 무관한 HTTP 교환 본체.
	template<typename Stream>
	asio::awaitable<Http::ResponseHead> Exchange(Stream& stream, const Http::Request& req, Http::HeadFn& onHead, Http::ChunkFn& onChunk)
	{
		// 1. 요청 전송
		const std::string requestText = BuildRequestText(req);
		co_await asio::async_write(stream, asio::buffer(requestText), asio::use_awaitable);

		// 2. 헤더 수신 (빈 줄까지)
		std::string buf;
		const size_t headLen = co_await asio::async_read_until(stream, asio::dynamic_buffer(buf), "\r\n\r\n", asio::use_awaitable);
		Http::ResponseHead head = ParseHead(std::string_view(buf).substr(0, headLen));
		buf.erase(0, headLen);   // buf 에 본문 일부가 이미 들어 있을 수 있다
		if (onHead) onHead(head);

		// 3. 본문 수신 — chunked / Content-Length / EOF 까지
		auto itTe = head.Headers.find("transfer-encoding");
		const bool chunked = (itTe != head.Headers.end()) && ToLower(itTe->second).find("chunked") != std::string::npos;

		// 필요한 만큼 buf 를 채운다. EOF 면 false.
		auto fill = [&](size_t need) -> asio::awaitable<bool>
		{
			while (buf.size() < need)
			{
				try
				{
					co_await asio::async_read(stream, asio::dynamic_buffer(buf), asio::transfer_at_least(1), asio::use_awaitable);
				}
				catch (const std::system_error& e)
				{
					if (e.code() == asio::error::eof || e.code() == asio::ssl::error::stream_truncated)
						co_return false;
					throw;
				}
			}
			co_return true;
		};

		if (chunked)
		{
			while (true)
			{
				// 크기 줄: "1a3\r\n" (16진수, 확장자는 ';' 뒤라 무시)
				size_t nl;
				while ((nl = buf.find("\r\n")) == std::string::npos)
				{
					if (!co_await fill(buf.size() + 1))
						throw std::runtime_error("chunked body truncated");
				}
				std::string_view sizeLine(buf.data(), nl);
				size_t semi = sizeLine.find(';');
				if (semi != std::string_view::npos) sizeLine = sizeLine.substr(0, semi);
				size_t chunkSize = 0;
				std::from_chars(sizeLine.data(), sizeLine.data() + sizeLine.size(), chunkSize, 16);
				buf.erase(0, nl + 2);

				if (chunkSize == 0)
					break;   // 마지막 청크. 트레일러는 읽지 않아도 된다 (Connection: close)

				if (!co_await fill(chunkSize + 2))
					throw std::runtime_error("chunked body truncated");
				onChunk(std::string_view(buf.data(), chunkSize));
				buf.erase(0, chunkSize + 2);
			}
		}
		else if (auto itCl = head.Headers.find("content-length"); itCl != head.Headers.end())
		{
			size_t total = 0;
			std::from_chars(itCl->second.data(), itCl->second.data() + itCl->second.size(), total);
			size_t delivered = 0;
			while (delivered < total)
			{
				if (buf.empty() && !co_await fill(1))
					break;
				const size_t take = std::min(buf.size(), total - delivered);
				onChunk(std::string_view(buf.data(), take));
				buf.erase(0, take);
				delivered += take;
			}
		}
		else
		{
			// 길이 정보 없음: 연결이 닫힐 때까지가 본문
			while (true)
			{
				if (!buf.empty())
				{
					onChunk(buf);
					buf.clear();
				}
				if (!co_await fill(1))
					break;
			}
		}

		co_return head;
	}

	asio::awaitable<Http::ResponseHead> Run(Http::Request req, Http::HeadFn onHead, Http::ChunkFn onChunk)
	{
		auto executor = co_await asio::this_coro::executor;
		const bool tls = (req.Scheme == "https");
		if (req.Port.empty())
			req.Port = tls ? "443" : "80";

		asio::ip::tcp::resolver resolver(executor);
		auto endpoints = co_await resolver.async_resolve(req.Host, req.Port, asio::use_awaitable);

		if (tls)
		{
			asio::ssl::context ctx(asio::ssl::context::tls_client);
			ctx.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
			                asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
			AddWindowsRootCerts(ctx);
			ctx.set_verify_mode(asio::ssl::verify_peer);

			SslStream stream(executor, ctx);
			stream.set_verify_callback(asio::ssl::host_name_verification(req.Host));
			if (!SSL_set_tlsext_host_name(stream.native_handle(), req.Host.c_str()))   // SNI
				throw std::runtime_error("SNI setup failed");

			co_await asio::async_connect(stream.next_layer(), endpoints, asio::use_awaitable);
			co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);

			Http::ResponseHead head = co_await Exchange(stream, req, onHead, onChunk);

			// 상대가 먼저 닫는 경우가 많아 shutdown 오류는 무시
			try { co_await stream.async_shutdown(asio::use_awaitable); } catch (...) {}
			co_return head;
		}
		else
		{
			asio::ip::tcp::socket socket(executor);
			co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
			Http::ResponseHead head = co_await Exchange(socket, req, onHead, onChunk);
			asio::error_code ec;
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
			co_return head;
		}
	}
}

namespace Http
{
	bool ParseBaseUrl(const std::string& url, std::string& outScheme, std::string& outHost, std::string& outPort)
	{
		const size_t sep = url.find("://");
		if (sep == std::string::npos) return false;
		outScheme = ToLower(url.substr(0, sep));
		std::string rest = url.substr(sep + 3);
		const size_t slash = rest.find('/');
		if (slash != std::string::npos) rest = rest.substr(0, slash);
		const size_t colon = rest.find(':');
		if (colon != std::string::npos)
		{
			outHost = rest.substr(0, colon);
			outPort = rest.substr(colon + 1);
		}
		else
		{
			outHost = rest;
			outPort.clear();
		}
		return (outScheme == "http" || outScheme == "https") && !outHost.empty();
	}

	asio::awaitable<ResponseHead> StreamPost(Request req, HeadFn onHead, ChunkFn onChunk)
	{
		auto executor = co_await asio::this_coro::executor;
		asio::steady_timer deadline(executor);
		deadline.expires_after(std::chrono::seconds(req.TimeoutSeconds));

		// 교환 전체와 타이머를 경쟁시킨다. 타이머가 먼저 끝나면 진행 중인 소켓 연산은 취소된다.
		auto result = co_await (Run(std::move(req), std::move(onHead), std::move(onChunk)) || deadline.async_wait(asio::use_awaitable));
		if (result.index() == 1)
			throw std::runtime_error("HTTP request timed out");
		co_return std::get<0>(result);
	}
}
