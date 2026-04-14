#include "StringUtils.h"
#include <Windows.h>

// CP949(ANSI) -> UTF-8 변환. 콘솔 입력을 protobuf에 넣을 때 사용.
std::string StringUtils::ToUtf8(const std::string& Ansi)
{
	if (Ansi.empty())
		return {};

	// CP949 -> UTF-16
	int32 iWideLen = ::MultiByteToWideChar(CP_ACP, 0, Ansi.c_str(), -1, nullptr, 0);
	std::wstring Wide(iWideLen, 0);
	::MultiByteToWideChar(CP_ACP, 0, Ansi.c_str(), -1, &Wide[0], iWideLen);

	// UTF-16 -> UTF-8
	return WideToUtf8(Wide);
}

// UTF-8 -> CP949(ANSI) 변환. protobuf 수신 데이터를 콘솔에 출력할 때 사용.
std::string StringUtils::FromUtf8(const std::string& Utf8)
{
	if (Utf8.empty())
		return {};

	// UTF-8 -> UTF-16
	std::wstring Wide = Utf8ToWide(Utf8);

	// UTF-16 -> CP949
	int32 iAnsiLen = ::WideCharToMultiByte(CP_ACP, 0, Wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string Ansi(iAnsiLen, 0);
	::WideCharToMultiByte(CP_ACP, 0, Wide.c_str(), -1, &Ansi[0], iAnsiLen, nullptr, nullptr);

	// null terminator 제거
	if (!Ansi.empty() && Ansi.back() == '\0')
		Ansi.pop_back();

	return Ansi;
}

// UTF-16(Wide) -> UTF-8 변환. MSSQL 결과를 protobuf에 넣을 때 사용.
std::string StringUtils::WideToUtf8(const std::wstring& Wide)
{
	if (Wide.empty())
		return {};

	int32 iUtf8Len = ::WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string Utf8(iUtf8Len, 0);
	::WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), -1, &Utf8[0], iUtf8Len, nullptr, nullptr);

	// null terminator 제거
	if (!Utf8.empty() && Utf8.back() == '\0')
		Utf8.pop_back();

	return Utf8;
}

// UTF-8 -> UTF-16(Wide) 변환. protobuf 데이터를 MSSQL에 넣을 때 사용.
std::wstring StringUtils::Utf8ToWide(const std::string& Utf8)
{
	if (Utf8.empty())
		return {};

	int32 iWideLen = ::MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), -1, nullptr, 0);
	std::wstring Wide(iWideLen, 0);
	::MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), -1, &Wide[0], iWideLen);

	// null terminator 제거
	if (!Wide.empty() && Wide.back() == L'\0')
		Wide.pop_back();

	return Wide;
}