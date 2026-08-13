#if MRUSTC_TARGET_HAS_NATIVE_F128
// Both clang and GCC expose IEEE binary128 as `__float128` on x86. Rust values
// remain raw bits outside these helpers, preserving the compiler's existing
// layout and transmute representation.
typedef __float128 f128_native;

static inline f128_native f128_decode(f128 v) {
    f128_native result;
    __builtin_memcpy(&result, &v.v, sizeof(result));
    return result;
}

static inline f128 f128_encode(f128_native v) {
    f128 result;
    __builtin_memcpy(&result.v, &v, sizeof(v));
    return result;
}

static inline f128 f128_add(f128 a, f128 b) { return f128_encode(f128_decode(a) + f128_decode(b)); }
static inline f128 f128_sub(f128 a, f128 b) { return f128_encode(f128_decode(a) - f128_decode(b)); }
static inline f128 f128_mul(f128 a, f128 b) { return f128_encode(f128_decode(a) * f128_decode(b)); }
static inline f128 f128_div(f128 a, f128 b) { return f128_encode(f128_decode(a) / f128_decode(b)); }
static inline f128 f128_mod(f128 a, f128 b) {
    f128_native lhs = f128_decode(a);
    f128_native rhs = f128_decode(b);
    return f128_encode(lhs - __builtin_truncf128(lhs / rhs) * rhs);
}
static inline f128 f128_neg(f128 v) { return f128_encode(-f128_decode(v)); }

static inline bool f128_eq(f128 a, f128 b) { return f128_decode(a) == f128_decode(b); }
static inline bool f128_ne(f128 a, f128 b) { return f128_decode(a) != f128_decode(b); }
static inline bool f128_gt(f128 a, f128 b) { return f128_decode(a) > f128_decode(b); }
static inline bool f128_ge(f128 a, f128 b) { return f128_decode(a) >= f128_decode(b); }
static inline bool f128_lt(f128 a, f128 b) { return f128_decode(a) < f128_decode(b); }
static inline bool f128_le(f128 a, f128 b) { return f128_decode(a) <= f128_decode(b); }

static inline f128 f128_abs(f128 v) { return f128_encode(__builtin_fabsf128(f128_decode(v))); }
static inline f128 f128_copysign(f128 a, f128 b) { return f128_encode(__builtin_copysignf128(f128_decode(a), f128_decode(b))); }
static inline f128 f128_floor(f128 v) { return f128_encode(__builtin_floorf128(f128_decode(v))); }
static inline f128 f128_ceil(f128 v) { return f128_encode(__builtin_ceilf128(f128_decode(v))); }
static inline f128 f128_round(f128 v) { return f128_encode(__builtin_roundf128(f128_decode(v))); }
static inline f128 f128_round_even(f128 v) {
    f128_native value = f128_decode(v);
    f128_native integral = __builtin_truncf128(value);
    f128_native fraction = value - integral;
    f128_native magnitude = __builtin_fabsf128(fraction);
    if (magnitude < 0.5Q) {
        return f128_encode(integral);
    }
    f128_native step = __builtin_copysignf128(1.0Q, value);
    if (magnitude > 0.5Q) {
        return f128_encode(integral + step);
    }
    f128_native half = __builtin_truncf128(integral / 2.0Q);
    return f128_encode(integral == half * 2.0Q ? integral : integral + step);
}
static inline f128 f128_trunc(f128 v) { return f128_encode(__builtin_truncf128(f128_decode(v))); }
static inline f128 f128_min(f128 a, f128 b) { return f128_encode(__builtin_fminf128(f128_decode(a), f128_decode(b))); }
static inline f128 f128_max(f128 a, f128 b) { return f128_encode(__builtin_fmaxf128(f128_decode(a), f128_decode(b))); }
static inline f128 f128_sqrt(f128 v) { return f128_encode(__builtin_sqrtf128(f128_decode(v))); }
static inline f128 f128_fma(f128 a, f128 b, f128 c) { return f128_encode(__builtin_fmaf128(f128_decode(a), f128_decode(b), f128_decode(c))); }
static inline f128 f128_pow(f128 a, f128 b) { return f128_encode(__builtin_powf128(f128_decode(a), f128_decode(b))); }
static inline f128 f128_exp(f128 v) { return f128_encode(__builtin_expf128(f128_decode(v))); }
static inline f128 f128_exp2(f128 v) { return f128_encode(__builtin_exp2f128(f128_decode(v))); }
static inline f128 f128_log(f128 v) { return f128_encode(__builtin_logf128(f128_decode(v))); }
static inline f128 f128_log10(f128 v) { return f128_encode(__builtin_log10f128(f128_decode(v))); }
static inline f128 f128_log2(f128 v) { return f128_encode(__builtin_log2f128(f128_decode(v))); }
static inline f128 f128_cos(f128 v) { return f128_encode(__builtin_cosf128(f128_decode(v))); }
static inline f128 f128_sin(f128 v) { return f128_encode(__builtin_sinf128(f128_decode(v))); }

static inline f128 f128_powi(f128 base, int32_t exponent) {
    f128_native factor = f128_decode(base);
    f128_native result = 1.0Q;
    bool negative = exponent < 0;
    uint32_t power = negative ? 0u - (uint32_t)exponent : (uint32_t)exponent;
    while (power != 0) {
        if (power & 1) {
            result *= factor;
        }
        factor *= factor;
        power >>= 1;
    }
    return f128_encode(negative ? 1.0Q / result : result);
}

#else
// Keep non-native targets compilable. They preserve the old runtime trap until
// a target supplies a real binary128 implementation.
typedef long double f128_native;
static f128_native f128_decode(f128) { abort(); }
static f128 f128_encode(f128_native) { abort(); }
static f128 f128_add(f128, f128) { abort(); }
static f128 f128_sub(f128, f128) { abort(); }
static f128 f128_mul(f128, f128) { abort(); }
static f128 f128_div(f128, f128) { abort(); }
static f128 f128_mod(f128, f128) { abort(); }
static f128 f128_neg(f128) { abort(); }
static bool f128_eq(f128, f128) { abort(); }
static bool f128_ne(f128, f128) { abort(); }
static bool f128_gt(f128, f128) { abort(); }
static bool f128_ge(f128, f128) { abort(); }
static bool f128_lt(f128, f128) { abort(); }
static bool f128_le(f128, f128) { abort(); }
static f128 f128_abs(f128) { abort(); }
static f128 f128_copysign(f128, f128) { abort(); }
static f128 f128_floor(f128) { abort(); }
static f128 f128_ceil(f128) { abort(); }
static f128 f128_round(f128) { abort(); }
static f128 f128_round_even(f128) { abort(); }
static f128 f128_trunc(f128) { abort(); }
static f128 f128_min(f128, f128) { abort(); }
static f128 f128_max(f128, f128) { abort(); }
static f128 f128_sqrt(f128) { abort(); }
static f128 f128_fma(f128, f128, f128) { abort(); }
static f128 f128_pow(f128, f128) { abort(); }
static f128 f128_exp(f128) { abort(); }
static f128 f128_exp2(f128) { abort(); }
static f128 f128_log(f128) { abort(); }
static f128 f128_log10(f128) { abort(); }
static f128 f128_log2(f128) { abort(); }
static f128 f128_cos(f128) { abort(); }
static f128 f128_sin(f128) { abort(); }
static f128 f128_powi(f128, int32_t) { abort(); }
#endif
