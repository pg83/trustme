#pragma once

#include <cstdint>

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

    F128(double v) {
        typedef union {
            double f;
            // 1.11.52
            uint64_t i;
        } double_cast;

        double_cast dc;
        dc.f = v;
        auto exp_sign_d = dc.i >> 52;
        // Trailing extend the exponent, so max stays max and min stays min
        auto exp_sign_q = exp_sign_d << (15 - 11) | ((exp_sign_d & 1) == 1 ? 0xF : 0);
        auto mantissa_d = dc.i & ((1LL << 52) - 1);
        auto mantissa_qh = mantissa_d >> 4;          // 4 bits of extra exponent
        auto mantissa_ql = (mantissa_d & 0xF) << 60; // Those lost 4 bits
        // Fill the tail of the mantissa with the final bit (so INF stays INF, and doesn't become a NaN)
        if (mantissa_d & 1) {
            mantissa_ql |= (1LL << 60) - 1;
        }
        this->lo = mantissa_ql;
        this->hi = (exp_sign_q << (112 - 64)) | mantissa_qh;
    }

    operator double() const {
        auto exp_sign_q = hi >> (112 - 64);
        auto mantissa_d = (hi & ((1LL << (112 - 64)) - 1)) | (lo >> 60);
        auto exp_sign_d = exp_sign_q >> 4;

        union {
            double f;
            uint64_t i;
        } dc;

        dc.i = (exp_sign_d << 52) | mantissa_d;
        return dc.f;
    }
};
