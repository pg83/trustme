#include "floats.h"

FloatValue parse_float_value(const char* text) {
    return Float128::parse_decimal(text);
}

bool floatValueIsNan(FloatValue value) {
    return value.is_nan();
}

bool floatValueIsInfinite(FloatValue value) {
    return value.is_infinite();
}

FloatValue floatValueAbs(FloatValue value) {
    return value.abs();
}

FloatValue floatValueTrunc(FloatValue value) {
    return value.trunc();
}

FloatValue floatValueFloor(FloatValue value) {
    return value.floor();
}

FloatValue floatValueCeil(FloatValue value) {
    return value.ceil();
}

FloatValue floatValueRound(FloatValue value) {
    return value.round();
}

FloatValue floatValueRoundEven(FloatValue value) {
    return value.round_even();
}

FloatValue floatValueRemainder(FloatValue lhs, FloatValue rhs) {
    return Float128::remainder(lhs, rhs);
}

FloatValue floatValueMinimumNumber(FloatValue lhs, FloatValue rhs) {
    return Float128::minimum_number(lhs, rhs);
}

FloatValue floatValueMaximumNumber(FloatValue lhs, FloatValue rhs) {
    return Float128::maximum_number(lhs, rhs);
}

FloatValue positive_nan_float_value() {
    return Float128::quiet_nan();
}

std::ostringstream&& operator<<(std::ostringstream&& os, const FloatValue& value) {
    static_cast<std::ostream&>(os) << value;
    return std::move(os);
}

F16::F16()
    : v(0)
{
}

F16::F16(float f) {
    union {
        float f;
        uint32_t i;
    } c;

    c.f = f;
    const auto sign = static_cast<uint16_t>((c.i >> 16) & 0x8000);
    const auto exponent = static_cast<int>((c.i >> 23) & 0xFF) - 127;
    auto mantissa = c.i & 0x7FFFFF;

    if (exponent == 128) {
        if (mantissa == 0) {
            v = sign | 0x7C00;
        } else {
            auto payload = static_cast<uint16_t>(mantissa >> 13);
            v = sign | 0x7C00 | payload | (payload == 0);
        }
    } else if (exponent > 15) {
        v = sign | 0x7C00;
    } else if (exponent >= -14) {
        auto rounded = mantissa + 0xFFF + ((mantissa >> 13) & 1);
        auto halfExponent = exponent + 15;
        if (rounded & 0x800000) {
            rounded = 0;
            halfExponent++;
        }
        if (halfExponent >= 31) {
            v = sign | 0x7C00;
        } else {
            v = sign | static_cast<uint16_t>(halfExponent << 10) | static_cast<uint16_t>(rounded >> 13);
        }
    } else if (exponent < -25) {
        v = sign;
    } else {
        mantissa |= 0x800000;
        const auto shift = static_cast<unsigned>(-exponent - 1);
        auto halfMantissa = mantissa >> shift;
        const auto remainder = mantissa & ((uint32_t(1) << shift) - 1);
        const auto halfway = uint32_t(1) << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (halfMantissa & 1))) {
            halfMantissa++;
        }
        v = sign | static_cast<uint16_t>(halfMantissa);
    }
}

F16::operator float() const {
    union {
        float f;
        uint32_t i;
    } dc;

    const auto sign = static_cast<uint32_t>(v & 0x8000) << 16;
    auto exponent = static_cast<uint32_t>((v >> 10) & 0x1F);
    auto mantissa = static_cast<uint32_t>(v & 0x03FF);
    if (exponent == 0) {
        if (mantissa == 0) {
            dc.i = sign;
            return dc.f;
        }
        int unbiased_exponent = -14;
        while ((mantissa & 0x0400) == 0) {
            mantissa <<= 1;
            unbiased_exponent--;
        }
        mantissa &= 0x03FF;
        exponent = static_cast<uint32_t>(unbiased_exponent + 127);
    } else if (exponent == 0x1F) {
        dc.i = sign | 0x7F800000 | (mantissa << 13);
        return dc.f;
    } else {
        exponent += 127 - 15;
    }
    dc.i = sign | (exponent << 23) | (mantissa << 13);
    return dc.f;
}

F128::F128()
    : lo(0)
    , hi(0)
{
}

F128::F128(FloatValue value)
    : lo(value.bitsLo())
    , hi(value.bitsHi())
{
}

F128::operator FloatValue() const {
    return Float128::fromBits(hi, lo);
}
