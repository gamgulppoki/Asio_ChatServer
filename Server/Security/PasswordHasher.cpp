#include "PasswordHasher.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>          // Windows CNG 난수 (BCryptGenRandom). 이름만 같고 bcrypt 해시와는 무관.
#pragma comment(lib, "bcrypt.lib")

extern "C"
{
#include "ThirdParty/bcrypt/crypt_blowfish.h"
}

namespace
{
	// bcrypt 가 요구하는 salt 원료 길이 (16바이트 → base64 22자)
	constexpr int kSaltBytes = 16;

	// 해시 출력 버퍼. "$2b$12$" 7 + salt 22 + hash 31 = 60 + NUL. 여유 있게.
	constexpr int kHashBufSize = 64;

	// 시스템 CSPRNG 에서 salt 원료를 받는다. 예측 가능한 난수(rand/time)를 쓰면
	// 같은 비밀번호가 같은 해시가 되어 레인보우 테이블에 뚫린다.
	bool FillRandom(unsigned char* buf, int size)
	{
		NTSTATUS status = BCryptGenRandom(nullptr, buf, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		return status == 0;   // STATUS_SUCCESS
	}

	// 타이밍 공격 방지용 상수 시간 비교. 앞부분만 다를 때 빨리 끝나면 한 글자씩 맞춰 나갈 수 있다.
	bool ConstantTimeEquals(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size())
			return false;
		unsigned char diff = 0;
		for (size_t i = 0; i < a.size(); ++i)
			diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
		return diff == 0;
	}
}

namespace PasswordHasher
{
	std::string Hash(const std::string& plain, int cost)
	{
		unsigned char saltBytes[kSaltBytes];
		if (!FillRandom(saltBytes, kSaltBytes))
			return {};

		// 1. "$2b$<cost>$<22자 salt>" 설정 문자열 생성
		char setting[kHashBufSize] = {};
		if (!_crypt_gensalt_blowfish_rn("$2b$", static_cast<unsigned long>(cost),
			reinterpret_cast<const char*>(saltBytes), kSaltBytes, setting, sizeof(setting)))
			return {};

		// 2. 설정 문자열 + 평문 → 60자 해시 (설정 문자열이 앞에 그대로 포함됨)
		char out[kHashBufSize] = {};
		if (!_crypt_blowfish_rn(plain.c_str(), setting, out, sizeof(out)))
			return {};

		return std::string(out);
	}

	bool Verify(const std::string& plain, const std::string& encoded)
	{
		if (!IsHash(encoded))
			return false;

		// 저장된 해시 자체가 setting 역할을 한다 (앞 29자에 버전·cost·salt 가 있음).
		// 같은 salt 로 다시 계산해서 전체 문자열이 같으면 비밀번호가 맞다.
		char out[kHashBufSize] = {};
		if (!_crypt_blowfish_rn(plain.c_str(), encoded.c_str(), out, sizeof(out)))
			return false;

		return ConstantTimeEquals(std::string(out), encoded);
	}

	bool IsHash(const std::string& stored)
	{
		return stored.size() >= 7 && stored[0] == '$' && stored[1] == '2';
	}
}
