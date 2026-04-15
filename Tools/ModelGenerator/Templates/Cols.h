#pragma once

#include "DB/Column.h"
#include "{{ struct_name }}Model.h"

// {{ table }} 테이블의 컬럼 메타 정보. Where 절 표현식 빌드용.
struct {{ struct_name }}Cols
{
{%- for col in columns %}
	{{ col.cpp_col_decl }}
{%- endfor %}
};