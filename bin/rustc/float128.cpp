#include "float128.h"

#include <string>
#include <vector>
#include <cassert>
#include <cstring>
#include <ostream>

namespace {
    using u128 = unsigned __int128;

    constexpr int significandBits = 112;
    constexpr int exponentBias = 16383;
    constexpr int min_exponent = -16382;
    constexpr int max_exponent = 16383;

    const u128 implicitBit = u128(1) << significandBits;
    const u128 fractionMask = implicitBit - 1;

    static int clz128(u128 v) {
        assert(v != 0);
        u64 hi = static_cast<u64>(v >> 64);
        if (hi != 0) {
            return __builtin_clzll(hi);
        }
        return 64 + __builtin_clzll(static_cast<u64>(v));
    }

    static int bitLength128(u128 v) {
        if (v == 0) {
            return 0;
        }
        return 128 - clz128(v);
    }

    enum class Kind {
        Zero,
        Finite,
        Infinity,
        NotANumber,
    };

    struct Unpacked {
        Kind kind;
        bool negative;
        i32 exponent;
        u128 significand;
    };

    static Unpacked unpack(u64 hi, u64 lo) {
        Unpacked r;
        r.negative = (hi >> 63) != 0;
        r.exponent = 0;
        r.significand = 0;
        const u32 biased = static_cast<u32>(hi >> 48) & 0x7FFF;
        const u128 fraction = (u128(hi & 0xFFFF'FFFF'FFFFull) << 64) | lo;
        if (biased == 0x7FFF) {
            r.kind = fraction != 0 ? Kind::NotANumber : Kind::Infinity;
        } else if (biased == 0) {
            if (fraction == 0) {
                r.kind = Kind::Zero;
            } else {
                r.kind = Kind::Finite;
                const int shift = clz128(fraction) - (128 - (significandBits + 1));
                r.significand = fraction << shift;
                r.exponent = min_exponent - shift;
            }
        } else {
            r.kind = Kind::Finite;
            r.significand = fraction | implicitBit;
            r.exponent = static_cast<i32>(biased) - exponentBias;
        }
        return r;
    }

    static Float128 packZero(bool negative) {
        return Float128::fromBits(negative ? 0x8000'0000'0000'0000ull : 0, 0);
    }

    static Float128 packFinite(bool negative, i32 exponent, u128 significand) {
        u64 biased;
        if (significand >= implicitBit) {
            assert(min_exponent <= exponent && exponent <= max_exponent);
            biased = static_cast<u64>(exponent + exponentBias);
            significand &= fractionMask;
        } else {
            assert(exponent == min_exponent);
            biased = 0;
        }
        const u64 hi = (negative ? 0x8000'0000'0000'0000ull : 0) | (biased << 48) | static_cast<u64>(significand >> 64);
        return Float128::fromBits(hi, static_cast<u64>(significand));
    }

    static u128 shiftRightSticky(u128 v, unsigned n, bool& sticky) {
        if (n == 0) {
            return v;
        }
        if (n >= 128) {
            sticky |= v != 0;
            return 0;
        }
        sticky |= (v & ((u128(1) << n) - 1)) != 0;
        return v >> n;
    }

    static Float128 canonicalNan() {
        return Float128::fromBits(0x7FFF'8000'0000'0000ull, 0);
    }

    static Float128 roundPack(bool negative, i32 exponent, u128 significand, bool sticky) {
        if (significand == 0) {
            return packZero(negative);
        }
        const int length = bitLength128(significand);
        if (length > significandBits + 2) {
            significand = shiftRightSticky(significand, static_cast<unsigned>(length - (significandBits + 2)), sticky);
            exponent += length - (significandBits + 2);
        } else if (length < significandBits + 2) {
            significand <<= (significandBits + 2) - length;
            exponent -= (significandBits + 2) - length;
        }
        if (exponent < min_exponent) {
            const i32 shift = min_exponent - exponent;
            significand = shiftRightSticky(significand, shift > 128 ? 128 : static_cast<unsigned>(shift), sticky);
            exponent = min_exponent;
        }
        const bool roundBit = (significand & 1) != 0;
        u128 result = significand >> 1;
        if (roundBit && (sticky || (result & 1) != 0)) {
            result += 1;
        }
        if ((result >> (significandBits + 1)) != 0) {
            result >>= 1;
            exponent += 1;
        }
        if (result == 0) {
            return packZero(negative);
        }
        if (exponent > max_exponent) {
            return Float128::infinity(negative);
        }
        return packFinite(negative, exponent, result);
    }

    static void multiplyFull(u128 a, u128 b, u64 out[4]) {
        const u64 a0 = static_cast<u64>(a);
        const u64 a1 = static_cast<u64>(a >> 64);
        const u64 b0 = static_cast<u64>(b);
        const u64 b1 = static_cast<u64>(b >> 64);
        const u128 p00 = u128(a0) * b0;
        const u128 p01 = u128(a0) * b1;
        const u128 p10 = u128(a1) * b0;
        const u128 p11 = u128(a1) * b1;
        const u128 mid = (p00 >> 64) + static_cast<u64>(p01) + static_cast<u64>(p10);
        const u128 high = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);
        out[0] = static_cast<u64>(p00);
        out[1] = static_cast<u64>(mid);
        out[2] = static_cast<u64>(high);
        out[3] = static_cast<u64>(high >> 64);
    }

    struct BigUint {
        std::vector<u64> limbs_;

        void trim();

        BigUint() = default;

        static BigUint fromU128(u128 v);

        bool isZero() const;

        void multiplyAddSmall(u64 factor, u64 addend);

        u64 divideSmall(u64 divisor);

        void shiftLeft(size_t bits);

        void shiftRightSticky(size_t bits, bool& sticky);

        size_t bitLength() const;

        u128 topBits(size_t bits, bool& sticky) const;

        std::string toDecimal() const;
    };

    constexpr u64 fivePow27 = 7'450'580'596'923'828'125ull;

    static void multiplyPow5(BigUint& v, u64 power) {
        while (power >= 27) {
            v.multiplyAddSmall(fivePow27, 0);
            power -= 27;
        }
        u64 factor = 1;
        for (u64 i = 0; i < power; i++) {
            factor *= 5;
        }
        if (factor != 1) {
            v.multiplyAddSmall(factor, 0);
        }
    }

    static void dividePow5Sticky(BigUint& v, u64 power, bool& sticky) {
        while (power >= 27) {
            sticky |= v.divideSmall(fivePow27) != 0;
            power -= 27;
        }
        u64 factor = 1;
        for (u64 i = 0; i < power; i++) {
            factor *= 5;
        }
        if (factor != 1) {
            sticky |= v.divideSmall(factor) != 0;
        }
    }

    static u64 roundToNarrow(i32 exponent, u128 significand, int targetMantissaBits, i32 targetMinExponent, i32 targetMaxExponent) {
        bool sticky = false;
        u128 sig = shiftRightSticky(significand, static_cast<unsigned>(significandBits - (targetMantissaBits + 1)), sticky);
        if (exponent < targetMinExponent) {
            const i32 shift = targetMinExponent - exponent;
            sig = shiftRightSticky(sig, shift > 128 ? 128 : static_cast<unsigned>(shift), sticky);
            exponent = targetMinExponent;
        }
        const bool roundBit = (sig & 1) != 0;
        u64 result = static_cast<u64>(sig >> 1);
        if (roundBit && (sticky || (result & 1) != 0)) {
            result += 1;
        }
        const u64 targetImplicit = u64(1) << targetMantissaBits;
        if (result >= targetImplicit * 2) {
            result >>= 1;
            exponent += 1;
        }
        const u64 bias = static_cast<u64>(1 - targetMinExponent);
        if (exponent > targetMaxExponent && result >= targetImplicit) {
            const u64 infiniteBiased = static_cast<u64>(targetMaxExponent) + bias + 1;
            return infiniteBiased << targetMantissaBits;
        }
        if (result == 0) {
            return 0;
        }
        if (result < targetImplicit) {
            return result;
        }
        const u64 biased = static_cast<u64>(exponent) + bias;
        return (biased << targetMantissaBits) | (result - targetImplicit);
    }

    static std::string digitsAt(const Unpacked& value, i64 lowestExponent10) {
        assert(value.kind == Kind::Finite);
        const i64 scale10 = -lowestExponent10 + 1;
        const i32 exponent2 = value.exponent - significandBits;
        bool sticky = false;
        BigUint work = BigUint::fromU128(value.significand);
        if (scale10 > 0) {
            multiplyPow5(work, static_cast<u64>(scale10));
        }
        const i64 shift2 = exponent2 + scale10;
        if (shift2 > 0) {
            work.shiftLeft(static_cast<size_t>(shift2));
        }
        if (scale10 < 0) {
            dividePow5Sticky(work, static_cast<u64>(-scale10), sticky);
        }
        if (shift2 < 0) {
            work.shiftRightSticky(static_cast<size_t>(-shift2), sticky);
        }
        std::string wide = work.toDecimal();
        const char extra = wide.back();
        wide.pop_back();
        while (!wide.empty() && wide.front() == '0') {
            wide.erase(wide.begin());
        }
        bool roundUp;
        if (extra > '5') {
            roundUp = true;
        } else if (extra < '5') {
            roundUp = false;
        } else if (sticky) {
            roundUp = true;
        } else {
            roundUp = !wide.empty() && ((wide.back() - '0') % 2) != 0;
        }
        if (roundUp) {
            int i = static_cast<int>(wide.size()) - 1;
            for (; i >= 0; i--) {
                if (wide[i] != '9') {
                    wide[i] += 1;
                    break;
                }
                wide[i] = '0';
            }
            if (i < 0) {
                wide.insert(wide.begin(), '1');
            }
        }
        return wide;
    }

    static std::string decimalDigits(const Unpacked& value, int digitCount, i32& decimalExponent) {
        assert(digitCount >= 1);
        i32 estimate = static_cast<i32>((static_cast<i64>(value.exponent) * 30103) / 100000);
        for (int attempt = 0;; attempt++) {
            assert(attempt < 4);
            std::string digits = digitsAt(value, static_cast<i64>(estimate) - digitCount + 1);
            if (static_cast<int>(digits.size()) != digitCount) {
                estimate += static_cast<int>(digits.size()) - digitCount;
                continue;
            }
            decimalExponent = estimate;
            return digits;
        }
    }

    static void appendExponent(std::string& out, i32 decimalExponent) {
        out.push_back('e');
        out.push_back(decimalExponent < 0 ? '-' : '+');
        std::string digits = std::to_string(decimalExponent < 0 ? -decimalExponent : decimalExponent);
        if (digits.size() < 2) {
            digits.insert(digits.begin(), '0');
        }
        out += digits;
    }

    static std::string formatScientific(const Unpacked& value, int precision) {
        std::string out;
        if (value.negative) {
            out.push_back('-');
        }
        if (value.kind == Kind::Zero) {
            out.push_back('0');
            if (precision > 0) {
                out.push_back('.');
                out.append(static_cast<size_t>(precision), '0');
            }
            appendExponent(out, 0);
            return out;
        }
        i32 decimalExponent = 0;
        const std::string digits = decimalDigits(value, precision + 1, decimalExponent);
        out.push_back(digits[0]);
        if (precision > 0) {
            out.push_back('.');
            out.append(digits, 1, std::string::npos);
        }
        appendExponent(out, decimalExponent);
        return out;
    }

    static std::string formatFixed(const Unpacked& value, int precision) {
        std::string out;
        if (value.negative) {
            out.push_back('-');
        }
        std::string digits;
        if (value.kind != Kind::Zero) {
            digits = digitsAt(value, -static_cast<i64>(precision));
        }
        if (static_cast<int>(digits.size()) <= precision) {
            out.push_back('0');
            if (precision > 0) {
                out.push_back('.');
                out.append(static_cast<size_t>(precision) - digits.size(), '0');
                out += digits;
            }
            return out;
        }
        const size_t whole = digits.size() - static_cast<size_t>(precision);
        out.append(digits, 0, whole);
        if (precision > 0) {
            out.push_back('.');
            out.append(digits, whole, std::string::npos);
        }
        return out;
    }

    static std::string formatDefault(const Unpacked& value, int precision) {
        const int significant = precision < 1 ? 1 : precision;
        std::string out;
        if (value.negative) {
            out.push_back('-');
        }
        if (value.kind == Kind::Zero) {
            out.push_back('0');
            return out;
        }
        i32 decimalExponent = 0;
        std::string digits = decimalDigits(value, significant, decimalExponent);
        while (digits.size() > 1 && digits.back() == '0') {
            digits.pop_back();
        }
        if (decimalExponent < -4 || decimalExponent >= significant) {
            out.push_back(digits[0]);
            if (digits.size() > 1) {
                out.push_back('.');
                out.append(digits, 1, std::string::npos);
            }
            appendExponent(out, decimalExponent);
        } else if (decimalExponent < 0) {
            out += "0.";
            out.append(static_cast<size_t>(-decimalExponent - 1), '0');
            out += digits;
        } else if (static_cast<size_t>(decimalExponent) + 1 >= digits.size()) {
            out += digits;
            out.append(static_cast<size_t>(decimalExponent) + 1 - digits.size(), '0');
        } else {
            out.append(digits, 0, static_cast<size_t>(decimalExponent) + 1);
            out.push_back('.');
            out.append(digits, static_cast<size_t>(decimalExponent) + 1, std::string::npos);
        }
        return out;
    }
}

Float128::Float128()
    : lo(0)
    , hi(0)
{
}

Float128::Float128(double value) {
    u64 bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    const bool negative = (bits >> 63) != 0;
    const u32 biased = static_cast<u32>(bits >> 52) & 0x7FF;
    const u64 fraction = bits & 0xF'FFFF'FFFF'FFFFull;
    if (biased == 0x7FF) {
        if (fraction != 0) {
            *this = canonicalNan();
            hi |= negative ? 0x8000'0000'0000'0000ull : 0;
        } else {
            *this = infinity(negative);
        }
        return;
    }
    if (biased == 0 && fraction == 0) {
        *this = packZero(negative);
        return;
    }
    i32 exponent;
    u64 significand;
    if (biased == 0) {
        const int shift = __builtin_clzll(fraction) - (63 - 52);
        significand = fraction << shift;
        exponent = -1022 - shift;
    } else {
        significand = fraction | (u64(1) << 52);
        exponent = static_cast<i32>(biased) - 1023;
    }
    *this = packFinite(negative, exponent, u128(significand) << (significandBits - 52));
}

Float128 Float128::fromBits(u64 hi, u64 lo) {
    Float128 r;
    r.hi = hi;
    r.lo = lo;
    return r;
}

Float128 Float128::quietNan() {
    return canonicalNan();
}

Float128 Float128::infinity(bool negative) {
    return fromBits((negative ? 0x8000'0000'0000'0000ull : 0) | 0x7FFF'0000'0000'0000ull, 0);
}

u64 Float128::bitsHi() const {
    return hi;
}

u64 Float128::bitsLo() const {
    return lo;
}

bool Float128::isNan() const {
    return unpack(hi, lo).kind == Kind::NotANumber;
}

bool Float128::isInfinite() const {
    return unpack(hi, lo).kind == Kind::Infinity;
}

u16 Float128::toF16Bits() const {
    const auto value = unpack(hi, lo);
    u16 bits = 0;
    switch (value.kind) {
        case Kind::Zero:
            bits = 0;
            break;
        case Kind::Infinity:
            bits = 0x7C00;
            break;
        case Kind::NotANumber:
            bits = 0x7E00;
            break;
        case Kind::Finite:
            bits = static_cast<u16>(roundToNarrow(value.exponent, value.significand, 10, -14, 15));
            break;
    }
    if (value.negative) {
        bits |= 0x8000;
    }
    return bits;
}

Float128::operator float() const {
    const auto value = unpack(hi, lo);
    u32 bits = 0;
    switch (value.kind) {
        case Kind::Zero:
            bits = 0;
            break;
        case Kind::Infinity:
            bits = 0x7F80'0000;
            break;
        case Kind::NotANumber:
            bits = 0x7FC0'0000;
            break;
        case Kind::Finite:
            bits = static_cast<u32>(roundToNarrow(value.exponent, value.significand, 23, -126, 127));
            break;
    }
    if (value.negative) {
        bits |= 0x8000'0000u;
    }
    float result;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(bits));
    return result;
}

Float128::operator double() const {
    const auto value = unpack(hi, lo);
    u64 bits = 0;
    switch (value.kind) {
        case Kind::Zero:
            bits = 0;
            break;
        case Kind::Infinity:
            bits = 0x7FF0'0000'0000'0000ull;
            break;
        case Kind::NotANumber:
            bits = 0x7FF8'0000'0000'0000ull;
            break;
        case Kind::Finite:
            bits = roundToNarrow(value.exponent, value.significand, 52, -1022, 1023);
            break;
    }
    if (value.negative) {
        bits |= 0x8000'0000'0000'0000ull;
    }
    double result;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(bits));
    return result;
}

Float128::operator i64() const {
    const auto value = unpack(hi, lo);
    switch (value.kind) {
        case Kind::Zero:
        case Kind::NotANumber:
            return 0;
        case Kind::Infinity:
            return value.negative ? INT64_MIN : INT64_MAX;
        case Kind::Finite:
            break;
    }
    if (value.exponent < 0) {
        return 0;
    }
    if (value.exponent >= 63) {
        if (value.negative && value.exponent == 63 && value.significand == implicitBit) {
            return INT64_MIN;
        }
        return value.negative ? INT64_MIN : INT64_MAX;
    }
    const u64 magnitude = static_cast<u64>(value.significand >> (significandBits - value.exponent));
    return value.negative ? -static_cast<i64>(magnitude) : static_cast<i64>(magnitude);
}

Float128::operator u64() const {
    const auto value = unpack(hi, lo);
    switch (value.kind) {
        case Kind::Zero:
        case Kind::NotANumber:
            return 0;
        case Kind::Infinity:
            return value.negative ? 0 : UINT64_MAX;
        case Kind::Finite:
            break;
    }
    if (value.negative || value.exponent < 0) {
        return 0;
    }
    if (value.exponent >= 64) {
        return UINT64_MAX;
    }
    return static_cast<u64>(value.significand >> (significandBits - value.exponent));
}

Float128 Float128::operator-() const {
    return fromBits(hi ^ 0x8000'0000'0000'0000ull, lo);
}

Float128 Float128::operator+(const Float128& other) const {
    const auto a = unpack(hi, lo);
    const auto b = unpack(other.hi, other.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return canonicalNan();
    }
    if (a.kind == Kind::Infinity || b.kind == Kind::Infinity) {
        if (a.kind == Kind::Infinity && b.kind == Kind::Infinity && a.negative != b.negative) {
            return canonicalNan();
        }
        return a.kind == Kind::Infinity ? *this : other;
    }
    if (a.kind == Kind::Zero && b.kind == Kind::Zero) {
        return packZero(a.negative && b.negative);
    }
    if (a.kind == Kind::Zero) {
        return other;
    }
    if (b.kind == Kind::Zero) {
        return *this;
    }
    const Unpacked* big = &a;
    const Unpacked* small = &b;
    if (a.exponent < b.exponent || (a.exponent == b.exponent && a.significand < b.significand)) {
        big = &b;
        small = &a;
    }
    const u128 bigSignificand = big->significand << 3;
    u128 smallSignificand = small->significand << 3;
    const i32 diff = big->exponent - small->exponent;
    bool jam = false;
    smallSignificand = shiftRightSticky(smallSignificand, diff > 128 ? 128 : static_cast<unsigned>(diff), jam);
    smallSignificand |= jam ? 1 : 0;
    u128 sum;
    if (big->negative == small->negative) {
        sum = bigSignificand + smallSignificand;
    } else {
        sum = bigSignificand - smallSignificand;
        if (sum == 0) {
            return packZero(false);
        }
    }
    return roundPack(big->negative, big->exponent - 2, sum, false);
}

Float128 Float128::operator-(const Float128& other) const {
    return *this + (-other);
}

Float128 Float128::operator*(const Float128& other) const {
    const auto a = unpack(hi, lo);
    const auto b = unpack(other.hi, other.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return canonicalNan();
    }
    const bool negative = a.negative != b.negative;
    if (a.kind == Kind::Infinity || b.kind == Kind::Infinity) {
        if (a.kind == Kind::Zero || b.kind == Kind::Zero) {
            return canonicalNan();
        }
        return infinity(negative);
    }
    if (a.kind == Kind::Zero || b.kind == Kind::Zero) {
        return packZero(negative);
    }
    u64 product[4];
    multiplyFull(a.significand, b.significand, product);
    u128 high = (u128(product[3]) << 64) | product[2];
    const u128 low = (u128(product[1]) << 64) | product[0];
    const int keep = 128 - bitLength128(high);
    high = (high << keep) | (low >> (128 - keep));
    const bool sticky = (low << keep) != 0;
    return roundPack(negative, a.exponent + b.exponent - 96 - keep + 113, high, sticky);
}

Float128 Float128::operator/(const Float128& other) const {
    const auto a = unpack(hi, lo);
    const auto b = unpack(other.hi, other.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return canonicalNan();
    }
    const bool negative = a.negative != b.negative;
    if (a.kind == Kind::Infinity) {
        if (b.kind == Kind::Infinity) {
            return canonicalNan();
        }
        return infinity(negative);
    }
    if (b.kind == Kind::Infinity) {
        return packZero(negative);
    }
    if (b.kind == Kind::Zero) {
        if (a.kind == Kind::Zero) {
            return canonicalNan();
        }
        return infinity(negative);
    }
    if (a.kind == Kind::Zero) {
        return packZero(negative);
    }
    u128 remainder = a.significand;
    u128 quotient = 0;
    for (int i = 0; i < 115; i++) {
        quotient <<= 1;
        if (remainder >= b.significand) {
            remainder -= b.significand;
            quotient |= 1;
        }
        remainder <<= 1;
    }
    return roundPack(negative, a.exponent - b.exponent - 1, quotient, remainder != 0);
}

bool Float128::operator==(const Float128& other) const {
    const auto a = unpack(hi, lo);
    const auto b = unpack(other.hi, other.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return false;
    }
    if (a.kind == Kind::Zero && b.kind == Kind::Zero) {
        return true;
    }
    return hi == other.hi && lo == other.lo;
}

bool Float128::operator!=(const Float128& other) const {
    const auto a = unpack(hi, lo);
    const auto b = unpack(other.hi, other.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return true;
    }
    return !(*this == other);
}

bool Float128::operator<(const Float128& other) const {
    const auto a = unpack(hi, lo);
    const auto b = unpack(other.hi, other.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return false;
    }
    if (a.kind == Kind::Infinity || b.kind == Kind::Infinity) {
        if (a.kind == Kind::Infinity && b.kind == Kind::Infinity) {
            return a.negative && !b.negative;
        }
        if (a.kind == Kind::Infinity) {
            return a.negative;
        }
        return !b.negative;
    }
    if (a.kind == Kind::Zero && b.kind == Kind::Zero) {
        return false;
    }
    if (a.kind == Kind::Zero) {
        return !b.negative;
    }
    if (b.kind == Kind::Zero) {
        return a.negative;
    }
    if (a.negative != b.negative) {
        return a.negative;
    }
    if (a.exponent == b.exponent && a.significand == b.significand) {
        return false;
    }
    const bool magnitudeLess = a.exponent != b.exponent ? a.exponent < b.exponent : a.significand < b.significand;
    return a.negative ? !magnitudeLess : magnitudeLess;
}

bool Float128::operator<=(const Float128& other) const {
    return *this < other || *this == other;
}

bool Float128::operator>(const Float128& other) const {
    return other < *this;
}

bool Float128::operator>=(const Float128& other) const {
    return other < *this || *this == other;
}

Float128 Float128::abs() const {
    return fromBits(hi & ~0x8000'0000'0000'0000ull, lo);
}

Float128 Float128::trunc() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significandBits) {
        return *this;
    }
    if (value.exponent < 0) {
        return packZero(value.negative);
    }
    const u128 mask = (u128(1) << (significandBits - value.exponent)) - 1;
    return packFinite(value.negative, value.exponent, value.significand & ~mask);
}

Float128 Float128::floor() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significandBits) {
        return *this;
    }
    const Float128 truncated = trunc();
    if (!value.negative || *this == truncated) {
        return truncated;
    }
    return truncated - Float128(1.0);
}

Float128 Float128::ceil() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significandBits) {
        return *this;
    }
    const Float128 truncated = trunc();
    if (value.negative || *this == truncated) {
        return truncated;
    }
    return truncated + Float128(1.0);
}

Float128 Float128::round() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significandBits) {
        return *this;
    }
    if (value.exponent < -1) {
        return packZero(value.negative);
    }
    if (value.exponent == -1) {
        return value.negative ? Float128(-1.0) : Float128(1.0);
    }
    const u128 half = u128(1) << (significandBits - value.exponent - 1);
    const u128 fraction = value.significand & (half * 2 - 1);
    const Float128 truncated = trunc();
    if (fraction < half) {
        return truncated;
    }
    return value.negative ? truncated - Float128(1.0) : truncated + Float128(1.0);
}

Float128 Float128::roundEven() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significandBits) {
        return *this;
    }
    if (value.exponent < -1) {
        return packZero(value.negative);
    }
    if (value.exponent == -1) {
        if (value.significand == implicitBit) {
            return packZero(value.negative);
        }
        return value.negative ? Float128(-1.0) : Float128(1.0);
    }
    const u128 half = u128(1) << (significandBits - value.exponent - 1);
    const u128 fraction = value.significand & (half * 2 - 1);
    const Float128 truncated = trunc();
    bool roundAway;
    if (fraction < half) {
        roundAway = false;
    } else if (fraction > half) {
        roundAway = true;
    } else {
        roundAway = (value.significand & (half * 2)) != 0;
    }
    if (!roundAway) {
        return truncated;
    }
    return value.negative ? truncated - Float128(1.0) : truncated + Float128(1.0);
}

Float128 Float128::remainder(const Float128& numerator, const Float128& denominator) {
    const auto a = unpack(numerator.hi, numerator.lo);
    const auto b = unpack(denominator.hi, denominator.lo);
    if (a.kind == Kind::NotANumber || b.kind == Kind::NotANumber) {
        return canonicalNan();
    }
    if (a.kind == Kind::Infinity || b.kind == Kind::Zero) {
        return canonicalNan();
    }
    if (a.kind == Kind::Zero || b.kind == Kind::Infinity) {
        return numerator;
    }
    if (a.exponent < b.exponent) {
        return numerator;
    }
    u128 rem = a.significand;
    for (i32 i = a.exponent - b.exponent; i > 0; i--) {
        if (rem >= b.significand) {
            rem -= b.significand;
        }
        rem <<= 1;
    }
    if (rem >= b.significand) {
        rem -= b.significand;
    }
    if (rem == 0) {
        return packZero(a.negative);
    }
    const int shortfall = significandBits + 1 - bitLength128(rem);
    const i32 exponent = b.exponent - shortfall;
    if (exponent < min_exponent) {
        if (b.exponent >= min_exponent) {
            return packFinite(a.negative, min_exponent, rem << (b.exponent - min_exponent));
        }
        const i32 shift = min_exponent - b.exponent;
        assert(shift >= 128 || (rem & ((u128(1) << shift) - 1)) == 0);
        return packFinite(a.negative, min_exponent, rem >> shift);
    }
    return packFinite(a.negative, exponent, rem << shortfall);
}

Float128 Float128::minimumNumber(const Float128& a, const Float128& b) {
    if (a.isNan()) {
        return b;
    }
    if (b.isNan()) {
        return a;
    }
    if (a == b) {
        return (a.bitsHi() >> 63) != 0 ? a : b;
    }
    return a < b ? a : b;
}

Float128 Float128::maximumNumber(const Float128& a, const Float128& b) {
    if (a.isNan()) {
        return b;
    }
    if (b.isNan()) {
        return a;
    }
    if (a == b) {
        return (a.bitsHi() >> 63) != 0 ? b : a;
    }
    return a < b ? b : a;
}

Float128 Float128::parseDecimal(const char* text) {
    const char* cursor = text;
    BigUint digits;
    i64 fractionDigits = 0;
    i64 significantDigits = 0;
    assert(*cursor == '.' || ('0' <= *cursor && *cursor <= '9'));
    for (; '0' <= *cursor && *cursor <= '9'; cursor++) {
        digits.multiplyAddSmall(10, static_cast<u64>(*cursor - '0'));
        if (!digits.isZero()) {
            significantDigits += 1;
        }
    }
    if (*cursor == '.') {
        cursor++;
        for (; '0' <= *cursor && *cursor <= '9'; cursor++) {
            digits.multiplyAddSmall(10, static_cast<u64>(*cursor - '0'));
            fractionDigits += 1;
            if (!digits.isZero()) {
                significantDigits += 1;
            }
        }
    }
    i64 exponent10 = -fractionDigits;
    if (*cursor == 'e' || *cursor == 'E') {
        cursor++;
        bool exponentNegative = false;
        if (*cursor == '+' || *cursor == '-') {
            exponentNegative = *cursor == '-';
            cursor++;
        }
        i64 explicitExponent = 0;
        assert('0' <= *cursor && *cursor <= '9');
        for (; '0' <= *cursor && *cursor <= '9'; cursor++) {
            explicitExponent = explicitExponent * 10 + (*cursor - '0');
            if (explicitExponent > 1'000'000'000) {
                explicitExponent = 1'000'000'000;
            }
        }
        exponent10 += exponentNegative ? -explicitExponent : explicitExponent;
    }
    assert(*cursor == '\0');
    if (digits.isZero()) {
        return packZero(false);
    }
    const i64 topWeight = significantDigits + exponent10;
    if (topWeight > 4940) {
        return infinity(false);
    }
    if (topWeight < -4975) {
        return packZero(false);
    }
    bool sticky = false;
    size_t scaledUp = 0;
    if (exponent10 >= 0) {
        multiplyPow5(digits, static_cast<u64>(exponent10));
        digits.shiftLeft(static_cast<size_t>(exponent10));
    } else {
        const u64 power = static_cast<u64>(-exponent10);
        const size_t need = 116 + (static_cast<size_t>(power) * 3322 + 999) / 1000 + 1;
        const size_t have = digits.bitLength();
        scaledUp = need > have ? need - have : 0;
        digits.shiftLeft(scaledUp);
        dividePow5Sticky(digits, power, sticky);
        digits.shiftRightSticky(static_cast<size_t>(power), sticky);
    }
    const size_t length = digits.bitLength();
    const u128 significand = digits.topBits(114, sticky);
    const i64 exponent2 = static_cast<i64>(length < 114 ? 114 : length) - 1 - static_cast<i64>(scaledUp);
    return roundPack(false, static_cast<i32>(exponent2), significand, sticky);
}

::std::ostream& operator<<(::std::ostream& os, const Float128& value) {
    const auto unpacked = unpack(value.hi, value.lo);
    if (unpacked.kind == Kind::NotANumber) {
        return os << (unpacked.negative ? "-nan" : "nan");
    }
    if (unpacked.kind == Kind::Infinity) {
        return os << (unpacked.negative ? "-inf" : "inf");
    }
    const int precision = static_cast<int>(os.precision());
    const auto field = os.flags() & ::std::ios_base::floatfield;
    std::string text;
    if (field == ::std::ios_base::scientific) {
        text = formatScientific(unpacked, precision < 0 ? 6 : precision);
    } else if (field == ::std::ios_base::fixed) {
        text = formatFixed(unpacked, precision < 0 ? 6 : precision);
    } else {
        text = formatDefault(unpacked, precision < 0 ? 6 : precision);
    }
    return os << text;
}

auto BigUint::trim() -> void {
    while (!limbs_.empty() && limbs_.back() == 0) {
        limbs_.pop_back();
    }
}

auto BigUint::fromU128(u128 v) -> BigUint {
    BigUint r;
    if (v != 0) {
        r.limbs_.push_back(static_cast<u64>(v));
        if ((v >> 64) != 0) {
            r.limbs_.push_back(static_cast<u64>(v >> 64));
        }
    }
    return r;
}

auto BigUint::isZero() const -> bool {
    return limbs_.empty();
}

auto BigUint::multiplyAddSmall(u64 factor, u64 addend) -> void {
    u128 carry = addend;
    for (auto& limb : limbs_) {
        const u128 v = u128(limb) * factor + carry;
        limb = static_cast<u64>(v);
        carry = v >> 64;
    }
    while (carry != 0) {
        limbs_.push_back(static_cast<u64>(carry));
        carry >>= 64;
    }
    trim();
}

auto BigUint::divideSmall(u64 divisor) -> u64 {
    assert(divisor != 0);
    u128 remainder = 0;
    for (size_t i = limbs_.size(); i-- > 0;) {
        const u128 cur = (remainder << 64) | limbs_[i];
        limbs_[i] = static_cast<u64>(cur / divisor);
        remainder = cur % divisor;
    }
    trim();
    return static_cast<u64>(remainder);
}

auto BigUint::shiftLeft(size_t bits) -> void {
    if (isZero() || bits == 0) {
        return;
    }
    const size_t whole = bits / 64;
    const unsigned rest = bits % 64;
    const size_t oldSize = limbs_.size();
    limbs_.resize(oldSize + whole + (rest != 0 ? 1 : 0), 0);
    for (size_t i = oldSize; i-- > 0;) {
        const u64 limb = limbs_[i];
        if (rest != 0) {
            limbs_[i + whole + 1] |= limb >> (64 - rest);
            limbs_[i + whole] = limb << rest;
        } else {
            limbs_[i + whole] = limb;
        }
        if (i < whole) {
            limbs_[i] = 0;
        }
    }
    for (size_t i = 0; i < whole && i < oldSize; i++) {
        limbs_[i] = 0;
    }
    trim();
}

auto BigUint::shiftRightSticky(size_t bits, bool& sticky) -> void {
    if (bits == 0) {
        return;
    }
    const size_t whole = bits / 64;
    const unsigned rest = bits % 64;
    if (whole >= limbs_.size()) {
        sticky |= !isZero();
        limbs_.clear();
        return;
    }
    for (size_t i = 0; i < whole; i++) {
        sticky |= limbs_[i] != 0;
    }
    if (rest != 0) {
        sticky |= (limbs_[whole] & ((u64(1) << rest) - 1)) != 0;
    }
    const size_t newSize = limbs_.size() - whole;
    for (size_t i = 0; i < newSize; i++) {
        u64 v = limbs_[i + whole] >> rest;
        if (rest != 0 && i + whole + 1 < limbs_.size()) {
            v |= limbs_[i + whole + 1] << (64 - rest);
        }
        limbs_[i] = v;
    }
    limbs_.resize(newSize);
    trim();
}

auto BigUint::bitLength() const -> size_t {
    if (limbs_.empty()) {
        return 0;
    }
    return limbs_.size() * 64 - static_cast<size_t>(__builtin_clzll(limbs_.back()));
}

auto BigUint::topBits(size_t bits, bool& sticky) const -> u128 {
    assert(bits <= 128);
    const size_t length = bitLength();
    BigUint copy = *this;
    if (length > bits) {
        copy.shiftRightSticky(length - bits, sticky);
    }
    u128 v = 0;
    for (size_t i = copy.limbs_.size(); i-- > 0;) {
        v = (v << 64) | copy.limbs_[i];
    }
    return v;
}

auto BigUint::toDecimal() const -> std::string {
    if (isZero()) {
        return "0";
    }
    std::string reversed;
    BigUint copy = *this;
    while (!copy.isZero()) {
        u64 chunk = copy.divideSmall(10'000'000'000'000'000'000ull);
        const bool more = !copy.isZero();
        for (int i = 0; i < 19 && (more || chunk != 0); i++) {
            reversed.push_back(static_cast<char>('0' + chunk % 10));
            chunk /= 10;
        }
    }
    return std::string(reversed.rbegin(), reversed.rend());
}
