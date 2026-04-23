#pragma once

#include <string>
#include "Types.h"

// 쿼리 WHERE 절의 단일 조건. column / op / value 로 구성.
struct Condition
{
    std::string column;
    std::string op;
    DbValue     value;
};
