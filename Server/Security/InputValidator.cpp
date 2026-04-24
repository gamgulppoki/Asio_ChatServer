#include "InputValidator.h"
#include <regex>
#include <cctype>

// 이름: 2~60바이트 + ASCII 공백(스페이스/탭 등) 금지.
// 공백 금지는 귓속말 파싱 ("/대상 메시지") 에서 첫 토큰을 대상으로 쓰기 위함.
bool InputValidator::IsValidName(const std::string& Name)
{
	if (Name.size() < 2 || Name.size() > 60)
		return false;

	for (char c : Name)
	{
		if (std::isspace(static_cast<unsigned char>(c)))
			return false;
	}
	return true;
}

// 이메일: 기본 형식 + 100자 이하
bool InputValidator::IsValidEmail(const std::string& Email)
{
	if (Email.size() > 100)
		return false;

	static const std::regex Pattern(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
	return std::regex_match(Email, Pattern);
}

// 비밀번호: 8~64자
bool InputValidator::IsValidPassword(const std::string& Password)
{
	return Password.size() >= 8 && Password.size() <= 64;
}