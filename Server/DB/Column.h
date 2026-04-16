#pragma once

#include "DBConnection.h"
#include <string>

// 표현식 빌드용 컬럼 디스크립터.
// DBModel의 ColumnDesc와 별개로, Where 절 표현식을 만들기 위한 메타 정보만 가진다.

// Where 절 표현식. SQL 조각(? 포함)과 값을 바인딩하는 람다를 들고 있는다.
struct Expression
{
    WString Sql;
    Function<void(DBConnection&, int32&)> Bind;
};

// 두 표현식을 AND로 합성한다. Bind 람다도 순서대로 체이닝된다.
inline Expression operator&&(const Expression& A, const Expression& B)
{
    return Expression{
        L"(" + A.Sql + L" AND " + B.Sql + L")",
        [A, B](DBConnection& Conn, int32& Idx) { A.Bind(Conn, Idx); B.Bind(Conn, Idx); }
    };
}

// 두 표현식을 OR로 합성한다. Bind 람다도 순서대로 체이닝된다.
inline Expression operator||(const Expression& A, const Expression& B)
{
    return Expression{
        L"(" + A.Sql + L" OR " + B.Sql + L")",
        [A, B](DBConnection& Conn, int32& Idx) { A.Bind(Conn, Idx); B.Bind(Conn, Idx); }
    };
}

// 일반 값 타입(int32, int64, float, ...) 컬럼.
template<typename T, typename Owner>
struct Column
{
    const WCHAR* Name;
    T Owner::* MemberPtr;

    Expression operator==(const T& Value) const { return Make(L"=",  Value); }
    Expression operator!=(const T& Value) const { return Make(L"<>", Value); }
    Expression operator< (const T& Value) const { return Make(L"<",  Value); }
    Expression operator<=(const T& Value) const { return Make(L"<=", Value); }
    Expression operator> (const T& Value) const { return Make(L">",  Value); }
    Expression operator>=(const T& Value) const { return Make(L">=", Value); }

private:
    Expression Make(const WCHAR* Op, const T& Value) const
    {
        return Expression{
            WString(Name) + L" " + Op + L" ?",
            [Value](DBConnection& Conn, int32& Idx) { Conn.BindParam(Idx++, Value); }
        };
    }
};

// WCHAR 배열 컬럼 (NVARCHAR 매핑).
template<int32 N, typename Owner>
struct StringColumn
{
    const WCHAR* Name;
    WCHAR (Owner::* MemberPtr)[N];

    Expression operator==(const WCHAR* Value) const { return Make(L"=",  Value); }
    Expression operator!=(const WCHAR* Value) const { return Make(L"<>", Value); }

private:
    Expression Make(const WCHAR* Op, const WCHAR* Value) const
    {
        return Expression{
            WString(Name) + L" " + Op + L" ?",
            [Value = WString(Value)](DBConnection& Conn, int32& Idx)
            {
                Conn.BindParam(Idx++, Value.c_str(), N);
            }
        };
    }
};