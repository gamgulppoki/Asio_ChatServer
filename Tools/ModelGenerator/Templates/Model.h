#pragma once

#include "DB/DBModel.h"

// {{ table }} 테이블에 대응하는 구조체.
struct {{ struct_name }}
{
{%- for col in columns %}
	{{ col.cpp_decl }}
{%- endfor %}
};

// {{ struct_name }} 구조체와 {{ table }} 테이블의 매핑을 설정한 DBModel을 생성한다.
inline DBModel<{{ struct_name }}> Create{{ struct_name }}Model(DBConnection& Conn)
{
	DBModel<{{ struct_name }}> Model(Conn, L"{{ table }}");
{%- for col in columns %}
	{{ col.cpp_addcol }}
{%- endfor %}
	return Model;
}