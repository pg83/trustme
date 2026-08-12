#pragma once

// Software implementation of IEEE 754 binary128 (1.15.112).
//
// The compiler represents every float value as binary128 so that f32/f64
// arithmetic sees no double rounding and f128 semantics are exact. This type
// implements that format in integer arithmetic: it has no dependency on
// `_Float128`, 128-bit libgcc/compiler-rt builtins, or glibc float128 APIs,
// so results are identical on every host toolchain.
//
// Semantics:
// - Arithmetic and conversions round to nearest, ties to even.
// - Operations that produce a NaN return the canonical quiet NaN (sign 0,
//   all exponent bits, top mantissa bit; payloads are not propagated).
// - Integer conversions truncate toward zero, saturate on overflow, and map
//   NaN to zero (Rust `as` semantics).

#include <cstdint>
#include <iosfwd>

class Float128 {
    uint64_t lo;
    uint64_t hi;

public:
    Float128();
    Float128(double value);  // implicit: exact widening

    static Float128 fromBits(uint64_t hi, uint64_t lo);
    // Parse `digits[.digits][(e|E)[+|-]digits]`, correctly rounded
    static Float128 parseDecimal(const char* text);
    static Float128 quietNan();
    static Float128 infinity(bool negative);

    uint64_t bitsHi() const;
    uint64_t bitsLo() const;

    bool isNan() const;
    bool isInfinite() const;

    // Correctly rounded narrowing
    explicit operator float() const;
    explicit operator double() const;
    // Truncating, saturating; NaN -> 0
    explicit operator int64_t() const;
    explicit operator uint64_t() const;

    Float128 operator-() const;
    Float128 operator+(const Float128& other) const;
    Float128 operator-(const Float128& other) const;
    Float128 operator*(const Float128& other) const;
    Float128 operator/(const Float128& other) const;

    // IEEE comparisons: any NaN operand compares unordered
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
    Float128 round() const;       // ties away from zero
    Float128 roundEven() const;  // ties to even

    // C `fmod` semantics: exact, result has the dividend's sign
    static Float128 remainder(const Float128& numerator, const Float128& denominator);
    // C `fmin`/`fmax` semantics: a single NaN operand loses
    static Float128 minimumNumber(const Float128& a, const Float128& b);
    static Float128 maximumNumber(const Float128& a, const Float128& b);

    // Honours the stream's `scientific` flag and precision; correctly
    // rounded decimal output (`printf` %e / %g shapes)
    friend ::std::ostream& operator<<(::std::ostream& os, const Float128& value);
};
