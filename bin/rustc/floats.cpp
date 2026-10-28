#include "floats.h"
#include "output.h"

#include <std/str/builder.h>

using namespace stl;

FloatValue parseFloatValue(const char* text) {
    return Float128::parseDecimal(text);
}

bool floatValueIsNan(FloatValue value) {
    return value.isNan();
}

bool floatValueIsInfinite(FloatValue value) {
    return value.isInfinite();
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
    return value.roundEven();
}

FloatValue floatValueRemainder(FloatValue lhs, FloatValue rhs) {
    return Float128::remainder(lhs, rhs);
}

FloatValue floatValueMinimumNumber(FloatValue lhs, FloatValue rhs) {
    return Float128::minimumNumber(lhs, rhs);
}

FloatValue floatValueMaximumNumber(FloatValue lhs, FloatValue rhs) {
    return Float128::maximumNumber(lhs, rhs);
}

FloatValue positiveNanFloatValue() {
    return Float128::quietNan();
}

std::string formatFloatValueForToken(FloatValue value) {
    std::string rv;
    for (int precision = 1; precision <= 36; precision++) {
        StringBuilder out;
        out << formatFloat128(value, FloatFormat::Default, precision);
        rv.assign(static_cast<const char*>(out.data()), out.length());
        if (parseFloatValue(rv.c_str()) == value) {
            break;
        }
    }
    if (rv.find_first_of(".eEni") == std::string::npos) {
        rv += ".0";
    }
    return rv;
}

F16::F16()
    : v(0)
{
}

F16::F16(FloatValue value)
    : v(value.toF16Bits())
{
}

F16::operator float() const {
    union {
        float f;
        u32 i;
    } dc;

    const auto sign = static_cast<u32>(v & 0x8000) << 16;
    auto exponent = static_cast<u32>((v >> 10) & 0x1F);
    auto mantissa = static_cast<u32>(v & 0x03FF);
    if (exponent == 0) {
        if (mantissa == 0) {
            dc.i = sign;
            return dc.f;
        }
        int unbiasedExponent = -14;
        while ((mantissa & 0x0400) == 0) {
            mantissa <<= 1;
            unbiasedExponent--;
        }
        mantissa &= 0x03FF;
        exponent = static_cast<u32>(unbiasedExponent + 127);
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
