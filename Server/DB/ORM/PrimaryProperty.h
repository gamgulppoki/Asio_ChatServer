#pragma once

#include <ostream>

// Primary Key 필드 전용 래퍼. Property 와 동일한 read-only 인터페이스를 제공하지만
// mutation 연산자 (operator=, ++/--, +=/-=/*=//=/%=) 가 없어서
// 사용자가 PK 를 직접 변경하는 것을 컴파일 시점에 차단.
// hydration 은 init() 으로만. codegen 이 이 타입을 발견하면 PK 로 자동 등록.
template<typename T>
class PrimaryProperty
{
public:

    // ===== Dirty Checking 인터페이스 =====
    void init(const T& v);
    const T& value() const;
    const T& origin() const;
    void clear_dirty();
    bool is_dirty() const;

    // ===== Read-only: 비교 =====
    bool operator==(const T& v) const requires requires(T a, T b) { a == b; } { return currentValue == v; }
    bool operator!=(const T& v) const requires requires(T a, T b) { a != b; } { return currentValue != v; }
    bool operator< (const T& v) const requires requires(T a, T b) { a <  b; } { return currentValue <  v; }
    bool operator<=(const T& v) const requires requires(T a, T b) { a <= b; } { return currentValue <= v; }
    bool operator> (const T& v) const requires requires(T a, T b) { a >  b; } { return currentValue >  v; }
    bool operator>=(const T& v) const requires requires(T a, T b) { a >= b; } { return currentValue >= v; }

    // ===== Read-only: 단항 =====
    bool operator!() const requires requires(T a) { !a; } { return !currentValue; }
    T    operator+() const requires requires(T a) { +a; } { return +currentValue; }
    T    operator-() const requires requires(T a) { -a; } { return -currentValue; }

    // ===== Read-only: 이항 산술 =====
    T operator+(const T& v) const requires requires(T a, T b) { a + b; } { return currentValue + v; }
    T operator-(const T& v) const requires requires(T a, T b) { a - b; } { return currentValue - v; }
    T operator*(const T& v) const requires requires(T a, T b) { a * b; } { return currentValue * v; }
    T operator/(const T& v) const requires requires(T a, T b) { a / b; } { return currentValue / v; }
    T operator%(const T& v) const requires requires(T a, T b) { a % b; } { return currentValue % v; }

    // ===== 출력 =====
    friend std::ostream& operator<<(std::ostream& os, const PrimaryProperty& p)
        requires requires(std::ostream& o, const T& a) { o << a; }
    {
        return os << p.currentValue;
    }

private:
    T currentValue;
    T originValue;
    bool bDirty = false;
};

template <typename T>
void PrimaryProperty<T>::init(const T& v)
{
    currentValue = v;
    originValue = v;
    bDirty = false;
}

template <typename T>
const T& PrimaryProperty<T>::value() const
{
    return currentValue;
}

template <typename T>
const T& PrimaryProperty<T>::origin() const
{
    return originValue;
}

template <typename T>
void PrimaryProperty<T>::clear_dirty()
{
    originValue = currentValue;
    bDirty = false;
}

template <typename T>
bool PrimaryProperty<T>::is_dirty() const
{
    return bDirty;
}