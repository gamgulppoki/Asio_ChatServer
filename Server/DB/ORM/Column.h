#pragma once
#include <string>
#include "Condition.h"
// 여기서 operation을 인식하고
// 그에 맞는 query문을 만들어주어야 함

// primary template 선언만. 특수화는 codegen이 EntitiesGenerated.h에 생성.
template<typename T> struct Col;

template<typename T>
struct ColumnRef
{
    std::string name;

    Condition make(const char* op, T value) const
    {
        Condition c;
        c.column = name;
        c.op     = op;
        c.value  = DbValue{value};
        return c;
    }

    Condition operator==(T v) const { return make("=",  v); }
    Condition operator!=(T v) const { return make("<>", v); }
    Condition operator< (T v) const { return make("<",  v); }
    Condition operator<=(T v) const { return make("<=", v); }
    Condition operator> (T v) const { return make(">",  v); }
    Condition operator>=(T v) const { return make(">=", v); }
};

