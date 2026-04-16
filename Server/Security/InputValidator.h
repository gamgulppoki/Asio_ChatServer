#pragma once

#include <string>

// 회원가입/로그인 입력값의 형식을 검증한다.
class InputValidator
{
public:
	// 이름: 2~60바이트 (UTF-8 한글은 3바이트/자, 20자 = 60바이트)
	static bool IsValidName(const std::string& Name);

	// 이메일: 기본 형식 검증 + 100자 이하
	static bool IsValidEmail(const std::string& Email);

	// 비밀번호: 8~64자
	static bool IsValidPassword(const std::string& Password);
};