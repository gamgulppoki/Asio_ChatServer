#pragma once

#include "DB/Column.h"
#include "UserModel.h"

// Users 테이블의 컬럼 메타 정보. Where 절 표현식 빌드용.
struct UserCols
{
	static inline Column<int32, User> Id{L"Id", &User::Id};
	static inline StringColumn<50, User> Name{L"Name", &User::Name};
	static inline StringColumn<100, User> Email{L"Email", &User::Email};
	static inline StringColumn<128, User> PasswordHash{L"PasswordHash", &User::PasswordHash};
};