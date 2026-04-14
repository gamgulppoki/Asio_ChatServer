#pragma once

#include "Types.h"
#include <string>

// 문자열 인코딩 변환 유틸리티.
// CP949(콘솔) <-> UTF-8(protobuf) <-> UTF-16(MSSQL, Windows API) 간 변환을 제공한다.
class StringUtils
{
public:
	static std::string ToUtf8(const std::string& Ansi);
	static std::string FromUtf8(const std::string& Utf8);
	static std::string WideToUtf8(const std::wstring& Wide);
	static std::wstring Utf8ToWide(const std::string& Utf8);
};