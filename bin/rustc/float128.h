#pragma once

#include <std/sys/types.h>

#include <iosfwd>
#include <cstdint>

class Float128 {
    u64 lo;
    u64 hi;

public:
    Float128();
    Float128(double value);

    static Float128 fromBits(u64 hi, u64 lo);

    static Float128 parseDecimal(const char* text);
    static Float128 quietNan();
    static Float128 infinity(bool negative);

    u64 bitsHi() const;
    u64 bitsLo() const;

    bool isNan() const;
    bool isInfinite() const;

    u16 toF16Bits() const;
    explicit operator float() const;
    explicit operator double() const;

    explicit operator i64() const;
    explicit operator u64() const;

    Float128 operator-() const;
    Float128 operator+(const Float128& other) const;
    Float128 operator-(const Float128& other) const;
    Float128 operator*(const Float128& other) const;
    Float128 operator/(const Float128& other) const;

    bool operator==(const Float128& other) const;
    bool operator!=(const Float128& other) const;
    bool operator<(const Float128& other) const;
    bool operator<=(const Float128& other) const;
    bool operator>(const Float128& other) const;
    bool operator>=(const Float128& other) const;

    Float128 abs() const;
    Float128 trunc() const;
    Float128 floor() const;
    Float128 ceil() const;
    Float128 round() const;
    Float128 roundEven() const;

    static Float128 remainder(const Float128& numerator, const Float128& denominator);

    static Float128 minimumNumber(const Float128& a, const Float128& b);
    static Float128 maximumNumber(const Float128& a, const Float128& b);

    friend ::std::ostream& operator<<(::std::ostream& os, const Float128& value);
};
