#pragma once

#include <string>
#include <variant>

using int8  = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;

using uint8  = unsigned __int8;
using uint16 = unsigned __int16;
using uint32 = unsigned __int32;
using uint64 = unsigned __int64;

using float32 = float;
using float64 = double;

// size32(T): 타입 크기를 int32로 캐스팅 (ODBC BindParam/BindCol용)
#define size32(type_or_value) static_cast<int32>(sizeof(type_or_value))


enum class TypeTag
{
    NONE,
    INT,
    DOUBLE,
    BOOL,
    STRING,
};

using DbValue = std::variant<std::monostate, int64, float64, bool, std::string>;