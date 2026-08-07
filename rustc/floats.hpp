#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <sstream>

class FloatValue {
    _Float128 m_value;

public:
    FloatValue()
        : m_value(0)
    {
    }

    FloatValue(_Float128 value)
        : m_value(value)
    {
    }

    operator _Float128() const {
        return m_value;
    }
};

inline FloatValue parse_float_value(const char* text) {
    return ::strtof128(text, nullptr);
}

inline bool float_value_is_nan(FloatValue value) {
    return __builtin_isnan(static_cast<_Float128>(value));
}

inline bool float_value_is_infinite(FloatValue value) {
    return __builtin_isinf(static_cast<_Float128>(value));
}

inline FloatValue float_value_remainder(FloatValue lhs, FloatValue rhs) {
    return ::fmodf128(lhs, rhs);
}

inline std::ostream& operator<<(std::ostream& os, const FloatValue& value) {
    return os << static_cast<long double>(static_cast<_Float128>(value));
}

inline std::ostringstream&& operator<<(std::ostringstream&& os, const FloatValue& value) {
    static_cast<std::ostream&>(os) << value;
    return std::move(os);
}

struct F16 {
    // 1.5.10
    uint16_t v;

    F16()
        : v(0)
    {
    }

    F16(float f) {
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
            auto half_exponent = exponent + 15;
            if (rounded & 0x800000) {
                rounded = 0;
                half_exponent++;
            }
            if (half_exponent >= 31) {
                v = sign | 0x7C00;
            } else {
                v = sign | static_cast<uint16_t>(half_exponent << 10) | static_cast<uint16_t>(rounded >> 13);
            }
        } else if (exponent < -25) {
            v = sign;
        } else {
            mantissa |= 0x800000;
            const auto shift = static_cast<unsigned>(-exponent - 1);
            auto half_mantissa = mantissa >> shift;
            const auto remainder = mantissa & ((uint32_t(1) << shift) - 1);
            const auto halfway = uint32_t(1) << (shift - 1);
            if (remainder > halfway || (remainder == halfway && (half_mantissa & 1))) {
                half_mantissa++;
            }
            v = sign | static_cast<uint16_t>(half_mantissa);
        }
    }

    operator float() const {
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
};

struct F128 {
    // 1.15.112
    uint64_t lo;
    uint64_t hi;

    F128()
        : lo(0)
        , hi(0)
    {
    }

    F128(FloatValue value) {
        auto native = static_cast<_Float128>(value);
        static_assert(sizeof(native) == sizeof(*this));
        std::memcpy(this, &native, sizeof(native));
    }

    operator FloatValue() const {
        _Float128 native;
        static_assert(sizeof(native) == sizeof(*this));
        std::memcpy(&native, this, sizeof(native));
        return FloatValue(native);
    }
};

inline FloatValue positive_nan_float_value() {
    F128 value;
    value.lo = 0;
    value.hi = 0x7fff800000000000;
    return value;
}
