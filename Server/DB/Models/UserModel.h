#pragma once

#include "DB/DBModel.h"

// Users 테이블에 대응하는 구조체.
struct User
{
	int32 Id = 0;
	WCHAR Name[50] = {};
	WCHAR Email[100] = {};
};

// User 구조체와 Users 테이블의 매핑을 설정한 DBModel을 생성한다.
inline DBModel<User> CreateUserModel(DBConnection& Conn)
{
	DBModel<User> Model(Conn, L"Users");
	Model.AddColumn(L"Id",    &User::Id, true);
	Model.AddColumn(L"Name",  &User::Name);
	Model.AddColumn(L"Email", &User::Email);
	return Model;
}