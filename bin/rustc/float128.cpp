#include "float128.h"

#include <string>
#include <vector>
#include <cassert>
#include <cstring>
#include <ostream>

namespace {
    using u128 = unsigned __int128;

    // binary128: 1 sign bit, 15 exponent bits, 112 fraction bits
    constexpr int significand_bits = 112;
    constexpr int exponentBias = 16383;
    constexpr int min_exponent = -16382;  // of normal values
    constexpr int max_exponent = 16383;

    const u128 implicit_bit = u128(1) << significand_bits;
    const u128 fractionMask = implicit_bit - 1;

    static int clz128(u128 v) {
        assert(v != 0);
        uint64_t hi = static_cast<uint64_t>(v >> 64);
        if (hi != 0) {
            return __builtin_clzll(hi);
        }
        return 64 + __builtin_clzll(static_cast<uint64_t>(v));
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
        // For Finite: value = significand * 2^(exponent - 112),
        // significand normalized to [2^112, 2^113)
        int32_t exponent;
        u128 significand;
    };

    static Unpacked unpack(uint64_t hi, uint64_t lo) {
        Unpacked r;
        r.negative = (hi >> 63) != 0;
        r.exponent = 0;
        r.significand = 0;
        const uint32_t biased = static_cast<uint32_t>(hi >> 48) & 0x7FFF;
        const u128 fraction = (u128(hi & 0xFFFF'FFFF'FFFFull) << 64) | lo;
        if (biased == 0x7FFF) {
            r.kind = fraction != 0 ? Kind::NotANumber : Kind::Infinity;
        } else if (biased == 0) {
            if (fraction == 0) {
                r.kind = Kind::Zero;
            } else {
                // Subnormal: value = fraction * 2^(min_exponent - 112)
                r.kind = Kind::Finite;
                const int shift = clz128(fraction) - (128 - (significand_bits + 1));
                r.significand = fraction << shift;
                r.exponent = min_exponent - shift;
            }
        } else {
            r.kind = Kind::Finite;
            r.significand = fraction | implicit_bit;
            r.exponent = static_cast<int32_t>(biased) - exponentBias;
        }
        return r;
    }

    static Float128 pack_zero(bool negative) {
        return Float128::fromBits(negative ? 0x8000'0000'0000'0000ull : 0, 0);
    }

    // significand in [0, 2^113); below 2^112 only with exponent == min_exponent
    static Float128 pack_finite(bool negative, int32_t exponent, u128 significand) {
        uint64_t biased;
        if (significand >= implicit_bit) {
            assert(min_exponent <= exponent && exponent <= max_exponent);
            biased = static_cast<uint64_t>(exponent + exponentBias);
            significand &= fractionMask;
        } else {
            assert(exponent == min_exponent);
            biased = 0;
        }
        const uint64_t hi = (negative ? 0x8000'0000'0000'0000ull : 0)
            | (biased << 48)
            | static_cast<uint64_t>(significand >> 64);
        return Float128::fromBits(hi, static_cast<uint64_t>(significand));
    }

    static u128 shift_right_sticky(u128 v, unsigned n, bool& sticky) {
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

    // Round to nearest, ties to even. Value = significand * 2^(exponent - 113)
    // for any nonzero significand (normalization happens here).
    static Float128 round_pack(bool negative, int32_t exponent, u128 significand, bool sticky) {
        if (significand == 0) {
            // A sticky remainder alone is far below half an ulp: rounds to zero
            return pack_zero(negative);
        }
        const int length = bitLength128(significand);
        if (length > significand_bits + 2) {
            significand = shift_right_sticky(significand, static_cast<unsigned>(length - (significand_bits + 2)), sticky);
            exponent += length - (significand_bits + 2);
        } else if (length < significand_bits + 2) {
            significand <<= (significand_bits + 2) - length;
            exponent -= (significand_bits + 2) - length;
        }
        // significand now in [2^113, 2^114): 113 result bits plus a round bit
        if (exponent < min_exponent) {
            const int32_t shift = min_exponent - exponent;
            significand = shift_right_sticky(significand, shift > 128 ? 128 : static_cast<unsigned>(shift), sticky);
            exponent = min_exponent;
        }
        const bool round_bit = (significand & 1) != 0;
        u128 result = significand >> 1;
        if (round_bit && (sticky || (result & 1) != 0)) {
            result += 1;
        }
        if ((result >> (significand_bits + 1)) != 0) {
            result >>= 1;
            exponent += 1;
        }
        if (result == 0) {
            return pack_zero(negative);
        }
        if (exponent > max_exponent) {
            return Float128::infinity(negative);
        }
        return pack_finite(negative, exponent, result);
    }

    // 128x128 -> 256 bit product as four 64-bit limbs (little-endian)
    static void multiply_full(u128 a, u128 b, uint64_t out[4]) {
        const uint64_t a0 = static_cast<uint64_t>(a);
        const uint64_t a1 = static_cast<uint64_t>(a >> 64);
        const uint64_t b0 = static_cast<uint64_t>(b);
        const uint64_t b1 = static_cast<uint64_t>(b >> 64);
        const u128 p00 = u128(a0) * b0;
        const u128 p01 = u128(a0) * b1;
        const u128 p10 = u128(a1) * b0;
        const u128 p11 = u128(a1) * b1;
        const u128 mid = (p00 >> 64) + static_cast<uint64_t>(p01) + static_cast<uint64_t>(p10);
        const u128 high = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);
        out[0] = static_cast<uint64_t>(p00);
        out[1] = static_cast<uint64_t>(mid);
        out[2] = static_cast<uint64_t>(high);
        out[3] = static_cast<uint64_t>(high >> 64);
    }

    // Arbitrary-precision unsigned integer, little-endian 64-bit limbs.
    // Only what decimal parsing and formatting need.
    class BigUint {
        std::vector<uint64_t> limbs_;

        void trim() {
            while (!limbs_.empty() && limbs_.back() == 0) {
                limbs_.pop_back();
            }
        }

    public:
        BigUint() = default;

        static BigUint fromU128(u128 v) {
            BigUint r;
            if (v != 0) {
                r.limbs_.push_back(static_cast<uint64_t>(v));
                if ((v >> 64) != 0) {
                    r.limbs_.push_back(static_cast<uint64_t>(v >> 64));
                }
            }
            return r;
        }

        bool is_zero() const {
            return limbs_.empty();
        }

        void multiply_add_small(uint64_t factor, uint64_t addend) {
            u128 carry = addend;
            for (auto& limb : limbs_) {
                const u128 v = u128(limb) * factor + carry;
                limb = static_cast<uint64_t>(v);
                carry = v >> 64;
            }
            while (carry != 0) {
                limbs_.push_back(static_cast<uint64_t>(carry));
                carry >>= 64;
            }
            trim();
        }

        // Returns the remainder
        uint64_t divideSmall(uint64_t divisor) {
            assert(divisor != 0);
            u128 remainder = 0;
            for (size_t i = limbs_.size(); i-- > 0;) {
                const u128 cur = (remainder << 64) | limbs_[i];
                limbs_[i] = static_cast<uint64_t>(cur / divisor);
                remainder = cur % divisor;
            }
            trim();
            return static_cast<uint64_t>(remainder);
        }

        void shift_left(size_t bits) {
            if (is_zero() || bits == 0) {
                return;
            }
            const size_t whole = bits / 64;
            const unsigned rest = bits % 64;
            const size_t old_size = limbs_.size();
            limbs_.resize(old_size + whole + (rest != 0 ? 1 : 0), 0);
            for (size_t i = old_size; i-- > 0;) {
                const uint64_t limb = limbs_[i];
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
            for (size_t i = 0; i < whole && i < old_size; i++) {
                limbs_[i] = 0;
            }
            trim();
        }

        // Discards the low `bits` bits; sets sticky if any of them was set
        void shift_right_sticky(size_t bits, bool& sticky) {
            if (bits == 0) {
                return;
            }
            const size_t whole = bits / 64;
            const unsigned rest = bits % 64;
            if (whole >= limbs_.size()) {
                sticky |= !is_zero();
                limbs_.clear();
                return;
            }
            for (size_t i = 0; i < whole; i++) {
                sticky |= limbs_[i] != 0;
            }
            if (rest != 0) {
                sticky |= (limbs_[whole] & ((uint64_t(1) << rest) - 1)) != 0;
            }
            const size_t new_size = limbs_.size() - whole;
            for (size_t i = 0; i < new_size; i++) {
                uint64_t v = limbs_[i + whole] >> rest;
                if (rest != 0 && i + whole + 1 < limbs_.size()) {
                    v |= limbs_[i + whole + 1] << (64 - rest);
                }
                limbs_[i] = v;
            }
            limbs_.resize(new_size);
            trim();
        }

        size_t bitLength() const {
            if (limbs_.empty()) {
                return 0;
            }
            return limbs_.size() * 64 - static_cast<size_t>(__builtin_clzll(limbs_.back()));
        }

        // The value truncated to its top `bits` bits; sets sticky if any
        // dropped bit was set
        u128 top_bits(size_t bits, bool& sticky) const {
            assert(bits <= 128);
            const size_t length = bitLength();
            BigUint copy = *this;
            if (length > bits) {
                copy.shift_right_sticky(length - bits, sticky);
            }
            u128 v = 0;
            for (size_t i = copy.limbs_.size(); i-- > 0;) {
                v = (v << 64) | copy.limbs_[i];
            }
            return v;
        }

        // Decimal digits, most significant first ("0" for zero)
        std::string to_decimal() const {
            if (is_zero()) {
                return "0";
            }
            std::string reversed;
            BigUint copy = *this;
            while (!copy.is_zero()) {
                uint64_t chunk = copy.divideSmall(10'000'000'000'000'000'000ull);
                const bool more = !copy.is_zero();
                for (int i = 0; i < 19 && (more || chunk != 0); i++) {
                    reversed.push_back(static_cast<char>('0' + chunk % 10));
                    chunk /= 10;
                }
            }
            return std::string(reversed.rbegin(), reversed.rend());
        }
    };

    // 5^27 is the largest power of five below 2^63
    constexpr uint64_t fivePow27 = 7'450'580'596'923'828'125ull;

    static void multiply_pow5(BigUint& v, uint64_t power) {
        while (power >= 27) {
            v.multiply_add_small(fivePow27, 0);
            power -= 27;
        }
        uint64_t factor = 1;
        for (uint64_t i = 0; i < power; i++) {
            factor *= 5;
        }
        if (factor != 1) {
            v.multiply_add_small(factor, 0);
        }
    }

    static void dividePow5Sticky(BigUint& v, uint64_t power, bool& sticky) {
        while (power >= 27) {
            sticky |= v.divideSmall(fivePow27) != 0;
            power -= 27;
        }
        uint64_t factor = 1;
        for (uint64_t i = 0; i < power; i++) {
            factor *= 5;
        }
        if (factor != 1) {
            sticky |= v.divideSmall(factor) != 0;
        }
    }

    // Round the value significand * 2^(exponent - 112) (significand
    // normalized to [2^112, 2^113)) into a float/double-shaped format and
    // return the raw target bits (without the sign bit).
    static uint64_t round_to_narrow(int32_t exponent, u128 significand, int target_mantissa_bits, int32_t target_min_exponent, int32_t target_max_exponent) {
        bool sticky = false;
        // Keep target_mantissa_bits + 2 bits: the result plus a round bit
        u128 sig = shift_right_sticky(significand, static_cast<unsigned>(significand_bits - (target_mantissa_bits + 1)), sticky);
        if (exponent < target_min_exponent) {
            const int32_t shift = target_min_exponent - exponent;
            sig = shift_right_sticky(sig, shift > 128 ? 128 : static_cast<unsigned>(shift), sticky);
            exponent = target_min_exponent;
        }
        const bool round_bit = (sig & 1) != 0;
        uint64_t result = static_cast<uint64_t>(sig >> 1);
        if (round_bit && (sticky || (result & 1) != 0)) {
            result += 1;
        }
        const uint64_t target_implicit = uint64_t(1) << target_mantissa_bits;
        if (result >= target_implicit * 2) {
            result >>= 1;
            exponent += 1;
        }
        const uint64_t bias = static_cast<uint64_t>(1 - target_min_exponent);
        if (exponent > target_max_exponent && result >= target_implicit) {
            // Overflow to infinity
            const uint64_t infinite_biased = static_cast<uint64_t>(target_max_exponent) + bias + 1;
            return infinite_biased << target_mantissa_bits;
        }
        if (result == 0) {
            return 0;
        }
        if (result < target_implicit) {
            return result;  // subnormal: biased exponent 0
        }
        const uint64_t biased = static_cast<uint64_t>(exponent) + bias;
        return (biased << target_mantissa_bits) | (result - target_implicit);
    }

    // floor(|value| / 10^lowest_exponent10) rounded to nearest (ties to
    // even), as decimal digits without leading zeros ("" means zero). This is
    // the shared exact core of every decimal output format.
    static std::string digitsAt(const Unpacked& value, int64_t lowest_exponent10) {
        assert(value.kind == Kind::Finite);
        // One extra decimal digit plus a sticky bit make the final rounding
        // exact: the extra digit separates above-half from below-half, and
        // sticky distinguishes a true tie
        const int64_t scale10 = -lowest_exponent10 + 1;
        const int32_t exponent2 = value.exponent - significand_bits;
        bool sticky = false;
        BigUint work = BigUint::fromU128(value.significand);
        if (scale10 > 0) {
            multiply_pow5(work, static_cast<uint64_t>(scale10));
        }
        const int64_t shift2 = exponent2 + scale10;
        if (shift2 > 0) {
            work.shift_left(static_cast<size_t>(shift2));
        }
        if (scale10 < 0) {
            dividePow5Sticky(work, static_cast<uint64_t>(-scale10), sticky);
        }
        if (shift2 < 0) {
            work.shift_right_sticky(static_cast<size_t>(-shift2), sticky);
        }
        std::string wide = work.to_decimal();
        const char extra = wide.back();
        wide.pop_back();
        while (!wide.empty() && wide.front() == '0') {
            wide.erase(wide.begin());
        }
        bool round_up;
        if (extra > '5') {
            round_up = true;
        } else if (extra < '5') {
            round_up = false;
        } else if (sticky) {
            round_up = true;
        } else {
            // True tie: to even ("" acts as the even digit zero)
            round_up = !wide.empty() && ((wide.back() - '0') % 2) != 0;
        }
        if (round_up) {
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

    // Correctly rounded conversion of |value| to `digit_count` significant
    // decimal digits. The first returned digit has weight
    // 10^decimal_exponent.
    static std::string decimalDigits(const Unpacked& value, int digitCount, int32_t& decimalExponent) {
        assert(digitCount >= 1);
        // log10(2) ~ 0.30103: first-digit estimate, corrected below
        int32_t estimate = static_cast<int32_t>((static_cast<int64_t>(value.exponent) * 30103) / 100000);
        for (int attempt = 0;; attempt++) {
            assert(attempt < 4);
            std::string digits = digitsAt(value, static_cast<int64_t>(estimate) - digitCount + 1);
            if (static_cast<int>(digits.size()) != digitCount) {
                estimate += static_cast<int>(digits.size()) - digitCount;
                continue;
            }
            decimalExponent = estimate;
            return digits;
        }
    }

    static void appendExponent(std::string& out, int32_t decimalExponent) {
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
        int32_t decimalExponent = 0;
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
            digits = digitsAt(value, -static_cast<int64_t>(precision));
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
        // printf %g: significant digits, trailing zeros trimmed
        const int significant = precision < 1 ? 1 : precision;
        std::string out;
        if (value.negative) {
            out.push_back('-');
        }
        if (value.kind == Kind::Zero) {
            out.push_back('0');
            return out;
        }
        int32_t decimalExponent = 0;
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
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    const bool negative = (bits >> 63) != 0;
    const uint32_t biased = static_cast<uint32_t>(bits >> 52) & 0x7FF;
    const uint64_t fraction = bits & 0xF'FFFF'FFFF'FFFFull;
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
        *this = pack_zero(negative);
        return;
    }
    int32_t exponent;
    uint64_t significand;
    if (biased == 0) {
        // Subnormal double: value = fraction * 2^(-1022 - 52)
        const int shift = __builtin_clzll(fraction) - (63 - 52);
        significand = fraction << shift;
        exponent = -1022 - shift;
    } else {
        significand = fraction | (uint64_t(1) << 52);
        exponent = static_cast<int32_t>(biased) - 1023;
    }
    // Exact: widen 53 significant bits to 113
    *this = pack_finite(negative, exponent, u128(significand) << (significand_bits - 52));
}

Float128 Float128::fromBits(uint64_t hi, uint64_t lo) {
    Float128 r;
    r.hi = hi;
    r.lo = lo;
    return r;
}

Float128 Float128::quiet_nan() {
    return canonicalNan();
}

Float128 Float128::infinity(bool negative) {
    return fromBits((negative ? 0x8000'0000'0000'0000ull : 0) | 0x7FFF'0000'0000'0000ull, 0);
}

uint64_t Float128::bitsHi() const {
    return hi;
}

uint64_t Float128::bitsLo() const {
    return lo;
}

bool Float128::is_nan() const {
    return unpack(hi, lo).kind == Kind::NotANumber;
}

bool Float128::is_infinite() const {
    return unpack(hi, lo).kind == Kind::Infinity;
}

Float128::operator float() const {
    const auto value = unpack(hi, lo);
    uint32_t bits = 0;
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
            bits = static_cast<uint32_t>(round_to_narrow(value.exponent, value.significand, 23, -126, 127));
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
    uint64_t bits = 0;
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
            bits = round_to_narrow(value.exponent, value.significand, 52, -1022, 1023);
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

Float128::operator int64_t() const {
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
        if (value.negative && value.exponent == 63 && value.significand == implicit_bit) {
            return INT64_MIN;
        }
        return value.negative ? INT64_MIN : INT64_MAX;
    }
    const uint64_t magnitude = static_cast<uint64_t>(value.significand >> (significand_bits - value.exponent));
    return value.negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
}

Float128::operator uint64_t() const {
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
    return static_cast<uint64_t>(value.significand >> (significand_bits - value.exponent));
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
        return pack_zero(a.negative && b.negative);
    }
    if (a.kind == Kind::Zero) {
        return other;
    }
    if (b.kind == Kind::Zero) {
        return *this;
    }
    // Three guard bits. The shifted-out remainder of the smaller operand is
    // jammed into the lowest bit; a jam bit can only be set when the
    // exponents differ by at least four, and then no left renormalization
    // happens, so the jam bit never becomes a value bit.
    const Unpacked* big = &a;
    const Unpacked* small = &b;
    if (a.exponent < b.exponent || (a.exponent == b.exponent && a.significand < b.significand)) {
        big = &b;
        small = &a;
    }
    const u128 bigSignificand = big->significand << 3;
    u128 small_significand = small->significand << 3;
    const int32_t diff = big->exponent - small->exponent;
    bool jam = false;
    small_significand = shift_right_sticky(small_significand, diff > 128 ? 128 : static_cast<unsigned>(diff), jam);
    small_significand |= jam ? 1 : 0;
    u128 sum;
    if (big->negative == small->negative) {
        sum = bigSignificand + small_significand;
    } else {
        sum = bigSignificand - small_significand;
        if (sum == 0) {
            return pack_zero(false);
        }
    }
    // value = sum * 2^(big->exponent - 115)
    return round_pack(big->negative, big->exponent - 2, sum, false);
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
        return pack_zero(negative);
    }
    uint64_t product[4];
    multiply_full(a.significand, b.significand, product);
    // product = sig_a * sig_b in [2^224, 2^226);
    // value = product * 2^(ea + eb - 224)
    u128 high = (u128(product[3]) << 64) | product[2];
    const u128 low = (u128(product[1]) << 64) | product[0];
    // high is in [2^96, 2^98): shift the top of `low` in to keep 128 bits
    const int keep = 128 - bitLength128(high);
    high = (high << keep) | (low >> (128 - keep));
    const bool sticky = (low << keep) != 0;
    // high = floor(product / 2^(128 - keep)), so
    // value = high * 2^(ea + eb - 96 - keep)
    return round_pack(negative, a.exponent + b.exponent - 96 - keep + 113, high, sticky);
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
        return pack_zero(negative);
    }
    if (b.kind == Kind::Zero) {
        if (a.kind == Kind::Zero) {
            return canonicalNan();
        }
        return infinity(negative);
    }
    if (a.kind == Kind::Zero) {
        return pack_zero(negative);
    }
    // Bitwise long division: after n rounds the quotient is
    // floor(sig_a * 2^(n-1) / sig_b), so 115 rounds give 113 result bits
    // plus a round bit for every quotient magnitude in (1/2, 2)
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
    // value = quotient * 2^(ea - eb - 114)
    return round_pack(negative, a.exponent - b.exponent - 1, quotient, remainder != 0);
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
            // -inf is below everything else; +inf is below nothing
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
    const bool magnitude_less = a.exponent != b.exponent
        ? a.exponent < b.exponent
        : a.significand < b.significand;
    return a.negative ? !magnitude_less : magnitude_less;
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
    if (value.kind != Kind::Finite || value.exponent >= significand_bits) {
        return *this;
    }
    if (value.exponent < 0) {
        return pack_zero(value.negative);
    }
    const u128 mask = (u128(1) << (significand_bits - value.exponent)) - 1;
    return pack_finite(value.negative, value.exponent, value.significand & ~mask);
}

Float128 Float128::floor() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significand_bits) {
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
    if (value.kind != Kind::Finite || value.exponent >= significand_bits) {
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
    if (value.kind != Kind::Finite || value.exponent >= significand_bits) {
        return *this;
    }
    if (value.exponent < -1) {
        return pack_zero(value.negative);
    }
    if (value.exponent == -1) {
        // Magnitude in [0.5, 1): ties away from zero
        return value.negative ? Float128(-1.0) : Float128(1.0);
    }
    const u128 half = u128(1) << (significand_bits - value.exponent - 1);
    const u128 fraction = value.significand & (half * 2 - 1);
    const Float128 truncated = trunc();
    if (fraction < half) {
        return truncated;
    }
    return value.negative ? truncated - Float128(1.0) : truncated + Float128(1.0);
}

Float128 Float128::round_even() const {
    const auto value = unpack(hi, lo);
    if (value.kind != Kind::Finite || value.exponent >= significand_bits) {
        return *this;
    }
    if (value.exponent < -1) {
        return pack_zero(value.negative);
    }
    if (value.exponent == -1) {
        // Magnitude in [0.5, 1): only exactly one half ties, to zero
        if (value.significand == implicit_bit) {
            return pack_zero(value.negative);
        }
        return value.negative ? Float128(-1.0) : Float128(1.0);
    }
    const u128 half = u128(1) << (significand_bits - value.exponent - 1);
    const u128 fraction = value.significand & (half * 2 - 1);
    const Float128 truncated = trunc();
    bool round_away;
    if (fraction < half) {
        round_away = false;
    } else if (fraction > half) {
        round_away = true;
    } else {
        round_away = (value.significand & (half * 2)) != 0;
    }
    if (!round_away) {
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
    // fmod is exact: reduce the aligned significand one bit of exponent
    // difference at a time
    u128 rem = a.significand;
    for (int32_t i = a.exponent - b.exponent; i > 0; i--) {
        if (rem >= b.significand) {
            rem -= b.significand;
        }
        rem <<= 1;
    }
    if (rem >= b.significand) {
        rem -= b.significand;
    }
    if (rem == 0) {
        return pack_zero(a.negative);
    }
    // value = rem * 2^(b.exponent - 112), |value| < |denominator|
    const int shortfall = significand_bits + 1 - bitLength128(rem);
    const int32_t exponent = b.exponent - shortfall;
    if (exponent < min_exponent) {
        // Subnormal result: express as fraction * 2^(min_exponent - 112).
        // fmod of two ulp multiples is an ulp multiple, so a right shift
        // here drops only zero bits.
        if (b.exponent >= min_exponent) {
            return pack_finite(a.negative, min_exponent, rem << (b.exponent - min_exponent));
        }
        const int32_t shift = min_exponent - b.exponent;
        assert(shift >= 128 || (rem & ((u128(1) << shift) - 1)) == 0);
        return pack_finite(a.negative, min_exponent, rem >> shift);
    }
    return pack_finite(a.negative, exponent, rem << shortfall);
}

Float128 Float128::minimum_number(const Float128& a, const Float128& b) {
    if (a.is_nan()) {
        return b;
    }
    if (b.is_nan()) {
        return a;
    }
    if (a == b) {
        // Prefer -0 over +0
        return (a.bitsHi() >> 63) != 0 ? a : b;
    }
    return a < b ? a : b;
}

Float128 Float128::maximum_number(const Float128& a, const Float128& b) {
    if (a.is_nan()) {
        return b;
    }
    if (b.is_nan()) {
        return a;
    }
    if (a == b) {
        // Prefer +0 over -0
        return (a.bitsHi() >> 63) != 0 ? b : a;
    }
    return a < b ? b : a;
}

Float128 Float128::parse_decimal(const char* text) {
    const char* cursor = text;
    BigUint digits;
    int64_t fractionDigits = 0;
    int64_t significant_digits = 0;
    assert(*cursor == '.' || ('0' <= *cursor && *cursor <= '9'));
    for (; '0' <= *cursor && *cursor <= '9'; cursor++) {
        digits.multiply_add_small(10, static_cast<uint64_t>(*cursor - '0'));
        if (!digits.is_zero()) {
            significant_digits += 1;
        }
    }
    if (*cursor == '.') {
        cursor++;
        for (; '0' <= *cursor && *cursor <= '9'; cursor++) {
            digits.multiply_add_small(10, static_cast<uint64_t>(*cursor - '0'));
            fractionDigits += 1;
            if (!digits.is_zero()) {
                significant_digits += 1;
            }
        }
    }
    int64_t exponent10 = -fractionDigits;
    if (*cursor == 'e' || *cursor == 'E') {
        cursor++;
        bool exponentNegative = false;
        if (*cursor == '+' || *cursor == '-') {
            exponentNegative = *cursor == '-';
            cursor++;
        }
        int64_t explicitExponent = 0;
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
    if (digits.is_zero()) {
        return pack_zero(false);
    }
    // Magnitude guards: beyond these the value is certainly out of range
    // (max f128 ~ 1.19e4932, half the smallest subnormal ~ 3.2e-4966)
    const int64_t top_weight = significant_digits + exponent10;
    if (top_weight > 4940) {
        return infinity(false);
    }
    if (top_weight < -4975) {
        return pack_zero(false);
    }
    bool sticky = false;
    size_t scaled_up = 0;
    if (exponent10 >= 0) {
        // digits * 10^e = digits * 5^e << e: exact
        multiply_pow5(digits, static_cast<uint64_t>(exponent10));
        digits.shift_left(static_cast<size_t>(exponent10));
    } else {
        // Divide by 10^-e = 5^-e * 2^-e with a sticky remainder. Scale up
        // first so the quotient keeps at least 116 bits and the round/sticky
        // bits below the top 114 are meaningful (log2(10) ~ 3.322).
        const uint64_t power = static_cast<uint64_t>(-exponent10);
        const size_t need = 116 + (static_cast<size_t>(power) * 3322 + 999) / 1000 + 1;
        const size_t have = digits.bitLength();
        scaled_up = need > have ? need - have : 0;
        digits.shift_left(scaled_up);
        dividePow5Sticky(digits, power, sticky);
        digits.shift_right_sticky(static_cast<size_t>(power), sticky);
    }
    // value = digits * 2^-scaled_up
    const size_t length = digits.bitLength();
    const u128 significand = digits.top_bits(114, sticky);
    // significand = floor(digits / 2^max(0, length - 114)), so
    // value = significand * 2^(max(length, 114) - 114 - scaled_up)
    const int64_t exponent2 = static_cast<int64_t>(length < 114 ? 114 : length) - 1 - static_cast<int64_t>(scaled_up);
    return round_pack(false, static_cast<int32_t>(exponent2), significand, sticky);
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
