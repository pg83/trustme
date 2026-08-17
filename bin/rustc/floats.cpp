#include "floats.h"

#include <iomanip>

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

::std::string formatFloatValueForToken(FloatValue value) {
    // A macro fragment is reparsed after expansion, so the text has to read
    // back as the same value -- but no more than that: `2.15` printed with the
    // 36 digits binary128 can need becomes `2.14999999999999999999999999999999992`,
    // and `concat!` would report that. Take the shortest that round-trips.
    ::std::string rv;
    for (int precision = 1; precision <= 36; precision++) {
        ::std::ostringstream os;
        os << ::std::setprecision(precision) << value;
        rv = os.str();
        if (parseFloatValue(rv.c_str()) == value) {
            break;
        }
    }
    // A float token always carries a decimal point in the source, and must keep
    // one: `4.0` rendered as `4` reparses as an integer, and `concat!`/
    // `stringify!` would report the wrong text.
    if (rv.find_first_of(".eEni") == ::std::string::npos) {
        rv += ".0";
    }
    return rv;
}

std::ostringstream&& operator<<(std::ostringstream&& os, const FloatValue& value) {
    static_cast<std::ostream&>(os) << value;
    return std::move(os);
}

F16::F16()
    : v(0)
{
}

F16::F16(FloatValue value)
    : v(value.toF16Bits())
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
        int unbiasedExponent = -14;
        while ((mantissa & 0x0400) == 0) {
            mantissa <<= 1;
            unbiasedExponent--;
        }
        mantissa &= 0x03FF;
        exponent = static_cast<uint32_t>(unbiasedExponent + 127);
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
