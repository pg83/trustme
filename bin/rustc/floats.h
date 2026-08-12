#pragma once

#include "float128.h"

#include <cstdint>
#include <sstream>

// Every float value in the compiler is a software binary128 (see
// float128.h), so f32/f64 arithmetic sees no double rounding, f128
// semantics are exact, and no host float128 toolchain support is needed.
using FloatValue = Float128;

FloatValue parse_float_value(const char* text);
bool float_value_is_nan(FloatValue value);
bool float_value_is_infinite(FloatValue value);
FloatValue float_value_abs(FloatValue value);
FloatValue float_value_trunc(FloatValue value);
FloatValue float_value_floor(FloatValue value);
FloatValue float_value_ceil(FloatValue value);
FloatValue float_value_round(FloatValue value);
FloatValue float_value_round_even(FloatValue value);
FloatValue float_value_remainder(FloatValue lhs, FloatValue rhs);
FloatValue float_value_minimum_number(FloatValue lhs, FloatValue rhs);
FloatValue float_value_maximum_number(FloatValue lhs, FloatValue rhs);
FloatValue positive_nan_float_value();

std::ostringstream&& operator<<(std::ostringstream&& os, const FloatValue& value);

// IEEE binary16 (1.5.10), converted through binary32
struct F16 {
    uint16_t v;

    F16();
    F16(float f);
    operator float() const;
};

// Raw binary128 bits, the on-disk and codegen representation
struct F128 {
    uint64_t lo;
    uint64_t hi;

    F128();
    F128(FloatValue value);
    operator FloatValue() const;
};
