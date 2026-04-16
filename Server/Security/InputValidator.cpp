#include "InputValidator.h"
#include <regex>

// 이름: 2~60바이트
bool InputValidator::IsValidName(const std::string& Name)
{
	return Name.size() >= 2 && Name.size() <= 60;
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