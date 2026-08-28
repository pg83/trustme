#pragma once

#include "float128.h"

#include <std/sys/types.h>

#include <cstdint>
#include <sstream>

using FloatValue = Float128;

FloatValue parseFloatValue(const char* text);
bool floatValueIsNan(FloatValue value);
bool floatValueIsInfinite(FloatValue value);
FloatValue floatValueAbs(FloatValue value);
FloatValue floatValueTrunc(FloatValue value);
FloatValue floatValueFloor(FloatValue value);
FloatValue floatValueCeil(FloatValue value);
FloatValue floatValueRound(FloatValue value);
FloatValue floatValueRoundEven(FloatValue value);
FloatValue floatValueRemainder(FloatValue lhs, FloatValue rhs);
FloatValue floatValueMinimumNumber(FloatValue lhs, FloatValue rhs);
FloatValue floatValueMaximumNumber(FloatValue lhs, FloatValue rhs);
FloatValue positiveNanFloatValue();
::std::string formatFloatValueForToken(FloatValue value);

std::ostringstream&& operator<<(std::ostringstream&& os, const FloatValue& value);

struct F16 {
    u16 v;

    F16();
    F16(float f);
    F16(FloatValue value);
    operator float() const;
};

struct F128 {
    u64 lo;
    u64 hi;

    F128();
    F128(FloatValue value);
    operator FloatValue() const;
};
