#pragma once

#include <ostream>

// 엔티티 필드를 감싸는 래퍼. Layer 6 Dirty Checking 의 기본 단위.
// 각 필드의 변경 여부를 실시간으로 추적해서 SaveChanges 시 MODIFIED
// 분기에서 dirty 컬럼만 UPDATE SET 절에 포함시키기 위함.
// raw 필드 사용은 codegen 단계에서 금지 (불변 조건).
template<typename T>
class Property
{
public:

    // ===== Dirty Checking 인터페이스 =====
    void init(const T& v);
    const T& value() const;
    const T& origin() const;
    void clear_dirty();
    bool is_dirty() const;

    // ===== Mutation: dirty 추적 =====
    Property& operator=(const T& v)
    {
        if (!bDirty)
        {
            originValue = currentValue;
        }
        currentValue = v;
        bDirty = true;
        return *this;
    }

    Property& operator++() requires requires(T a) { ++a; }
    {
        if (!bDirty)
        {
            originValue = currentValue;
        }
        ++currentValue;
        bDirty = true;
        return *this;
    }

    Property& operator--() requires requires(T a) { --a; }
    {
        if (!bDirty)
        {
            originValue = currentValue;
        }
        --currentValue;
        bDirty = true;
        return *this;
    }

    Property operator++(int) requires requires(T a) { a++; }
    {
        Property tmp = *this;
        ++(*this);
        return tmp;
    }

    Property operator--(int) requires requires(T a) { a--; }
    {
        Property tmp = *this;
        --(*this);
        return tmp;
    }

    Property& operator+=(const T& v) requires requires(T a, T b) { a += b; }
    {
        if (!bDirty) originValue = currentValue;
        currentValue += v;
        bDirty = true;
        return *this;
    }

    Property& operator-=(const T& v) requires requires(T a, T b) { a -= b; }
    {
        if (!bDirty) originValue = currentValue;
        currentValue -= v;
        bDirty = true;
        return *this;
    }

    Property& operator*=(const T& v) requires requires(T a, T b) { a *= b; }
    {
        if (!bDirty) originValue = currentValue;
        currentValue *= v;
        bDirty = true;
        return *this;
    }

    Property& operator/=(const T& v) requires requires(T a, T b) { a /= b; }
    {
        if (!bDirty) originValue = currentValue;
        currentValue /= v;
        bDirty = true;
        return *this;
    }

    Property& operator%=(const T& v) requires requires(T a, T b) { a %= b; }
    {
        if (!bDirty) originValue = currentValue;
        currentValue %= v;
        bDirty = true;
        return *this;
    }

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
    friend std::ostream& operator<<(std::ostream& os, const Property& p)
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
void Property<T>::init(const T& v)
{
    currentValue = v;
    originValue = v;
    bDirty = false;
}

template <typename T>
const T& Property<T>::value() const
{
    return currentValue;
}

template <typename T>
const T& Property<T>::origin() const
{
    return originValue;
}

template <typename T>
void Property<T>::clear_dirty()
{
    originValue = currentValue;
    bDirty = false;
}

template <typename T>
bool Property<T>::is_dirty() const
{
    return bDirty;
}
