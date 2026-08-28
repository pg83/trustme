/*
 * Tests for the C prelude that every generated program carries.
 *
 * The prelude is ordinary C, so it can be compiled straight into this test and
 * called directly, rather than only ever being reached through a compiled Rust
 * program. The emulated 128-bit integers are what these cover: that is the
 * configuration the compiler emits, and it is hand-written arithmetic.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <std/tst/ut.h>

using namespace stl;

namespace {
#define TRUSTME_CODEGEN_DISALLOW_EMPTY_STRUCTS 0
#define TRUSTME_TARGET_EMULATED_I128 1
#define TRUSTME_TARGET_U128_ALIGN 16
#define TRUSTME_TARGET_HAS_NATIVE_F128 1
#include "prelude.inc"
}
}

namespace {
    uint128_t u128(u64 hi, u64 lo) {
        return make128_raw(hi, lo);
    }

    int128_t i128(u64 hi, u64 lo) {
        return make128s_raw(hi, lo);
    }

    bool same(uint128_t a, uint128_t b) {
        return a.hi == b.hi && a.lo == b.lo;
    }

    bool sameS(int128_t a, int128_t b) {
        return a.hi == b.hi && a.lo == b.lo;
    }

    const uint128_t U128_MAX = make128_raw(UINT64_MAX, UINT64_MAX);
    const uint128_t U128_ZERO = make128(0);
}

STD_TEST_SUITE(PreludeInt128) {
    STD_TEST(testHalvesAreNamedNotOrdered) {
        const auto v = u128(0x0123456789ABCDEF, 0xFEDCBA9876543210);
        STD_INSIST(v.hi == 0x0123456789ABCDEF);
        STD_INSIST(v.lo == 0xFEDCBA9876543210);

        const auto asSigned = uint128_to_int128(v);
        STD_INSIST(asSigned.hi == v.hi);
        STD_INSIST(asSigned.lo == v.lo);
        STD_INSIST(same(int128_to_uint128(asSigned), v));

        STD_INSIST(make128(7).lo == 7 && make128(7).hi == 0);
        STD_INSIST(make128s(-1).hi == UINT64_MAX && make128s(-1).lo == UINT64_MAX);
    }

    STD_TEST(testAddCarry) {
        uint128_t out;
        STD_INSIST(!add128_o(make128(1), make128(2), &out) && same(out, make128(3)));

        STD_INSIST(!add128_o(u128(0, UINT64_MAX), make128(1), &out));
        STD_INSIST(same(out, u128(1, 0)));

        STD_INSIST(add128_o(U128_MAX, make128(1), &out) && same(out, U128_ZERO));

        const auto a = u128(UINT64_MAX, UINT64_MAX - 1);
        STD_INSIST(add128_o(a, U128_MAX, &out));
        STD_INSIST(same(out, u128(UINT64_MAX, UINT64_MAX - 2)));
        STD_INSIST(add128_o(U128_MAX, a, &out));
        STD_INSIST(same(out, u128(UINT64_MAX, UINT64_MAX - 2)));
    }

    STD_TEST(testSubBorrow) {
        uint128_t out;
        STD_INSIST(!sub128_o(make128(5), make128(3), &out) && same(out, make128(2)));
        STD_INSIST(sub128_o(U128_ZERO, make128(1), &out) && same(out, U128_MAX));

        STD_INSIST(!sub128_o(u128(1, 0), make128(1), &out));
        STD_INSIST(same(out, u128(0, UINT64_MAX)));

        STD_INSIST(sub128_o(u128(UINT64_MAX, 0), u128(UINT64_MAX, 1), &out));
        STD_INSIST(same(out, U128_MAX));
    }

    STD_TEST(testMulOverflow) {
        uint128_t out;
        STD_INSIST(!mul128_o(make128(3), make128(5), &out) && same(out, make128(15)));
        STD_INSIST(!mul128_o(U128_ZERO, U128_MAX, &out) && same(out, U128_ZERO));
        STD_INSIST(!mul128_o(U128_MAX, make128(1), &out) && same(out, U128_MAX));

        STD_INSIST(mul128_o(U128_MAX, make128(2), &out));
        STD_INSIST(same(out, u128(UINT64_MAX, UINT64_MAX - 1)));
        STD_INSIST(mul128_o(u128(0x8000000000000000, 0), make128(2), &out));
        STD_INSIST(same(out, U128_ZERO));

        STD_INSIST(!mul128_o(u128(0, UINT64_MAX), u128(0, UINT64_MAX), &out));
        STD_INSIST(same(out, u128(UINT64_MAX - 1, 1)));
    }

    STD_TEST(testDivMod) {
        uint128_t quotient, remainder;
        STD_INSIST(!div128_o(make128(17), make128(5), &quotient, &remainder));
        STD_INSIST(same(quotient, make128(3)) && same(remainder, make128(2)));

        STD_INSIST(!div128_o(U128_MAX, make128(3), &quotient, &remainder));
        STD_INSIST(same(quotient, u128(0x5555555555555555, 0x5555555555555555)));
        STD_INSIST(same(remainder, U128_ZERO));

        STD_INSIST(!div128_o(u128(1, 0), u128(0, 2), &quotient, &remainder));
        STD_INSIST(same(quotient, u128(0, 0x8000000000000000)));
        STD_INSIST(same(remainder, U128_ZERO));
    }

    STD_TEST(testShifts) {
        STD_INSIST(same(shl128(make128(1), 0), make128(1)));
        STD_INSIST(same(shl128(make128(1), 64), u128(1, 0)));
        STD_INSIST(same(shl128(make128(1), 127), u128(0x8000000000000000, 0)));
        STD_INSIST(same(shr128(u128(0x8000000000000000, 0), 127), make128(1)));
        STD_INSIST(same(shr128(u128(1, 0), 64), make128(1)));

        STD_INSIST(sameS(shr128s(make128s(-1), 64), make128s(-1)));
        STD_INSIST(sameS(shr128s(i128(0x8000000000000000, 0), 127), make128s(-1)));
        STD_INSIST(sameS(shr128s(make128s(8), 2), make128s(2)));
    }

    STD_TEST(testCompare) {
        STD_INSIST(cmp128(make128(1), make128(2)) < 0);
        STD_INSIST(cmp128(make128(2), make128(1)) > 0);
        STD_INSIST(cmp128(U128_MAX, U128_MAX) == 0);
        STD_INSIST(cmp128(u128(1, 0), u128(0, UINT64_MAX)) > 0);

        STD_INSIST(cmp128s(make128s(-1), make128s(1)) < 0);
        STD_INSIST(cmp128s(i128(0x8000000000000000, 0), make128s(0)) < 0);
        STD_INSIST(cmp128s(make128s(0), make128s(0)) == 0);
    }

    STD_TEST(testBitCounting) {
        STD_INSIST(intrinsic_ctlz_u128(U128_ZERO).lo == 128);
        STD_INSIST(intrinsic_ctlz_u128(make128(1)).lo == 127);
        STD_INSIST(intrinsic_ctlz_u128(u128(1, 0)).lo == 63);
        STD_INSIST(intrinsic_ctlz_u128(U128_MAX).lo == 0);

        STD_INSIST(intrinsic_cttz_u128(U128_ZERO).lo == 128);
        STD_INSIST(intrinsic_cttz_u128(make128(1)).lo == 0);
        STD_INSIST(intrinsic_cttz_u128(u128(1, 0)).lo == 64);
        STD_INSIST(intrinsic_cttz_u128(u128(0x8000000000000000, 0)).lo == 127);
    }

    STD_TEST(testReorderBits) {
        STD_INSIST(same(__trustme_bitrev128(U128_ZERO), U128_ZERO));
        STD_INSIST(same(__trustme_bitrev128(make128(1)), u128(0x8000000000000000, 0)));
        STD_INSIST(same(__trustme_bitrev128(U128_MAX), U128_MAX));
        STD_INSIST(same(__trustme_bitrev128(u128(0, 2)), u128(0x4000000000000000, 0)));

        STD_INSIST(same(__trustme_bswap128(U128_ZERO), U128_ZERO));
        STD_INSIST(same(__trustme_bswap128(make128(1)), u128(0x0100000000000000, 0)));
        STD_INSIST(same(__trustme_bswap128(u128(0x0102030405060708, 0x090A0B0C0D0E0F10)), u128(0x100F0E0D0C0B0A09, 0x0807060504030201)));
    }

    STD_TEST(testIntToFloat) {
        STD_INSIST(cast128_double(make128(0)) == 0.0);
        STD_INSIST(cast128_double(make128(11259375)) == 11259375.0);
        STD_INSIST(cast128_float(make128(11259375)) == 11259375.0f);
        STD_INSIST(cast128_double(u128(1, 0)) == 18446744073709551616.0);

        STD_INSIST(cast128s_double(make128s(-11259375)) == -11259375.0);
        STD_INSIST(cast128s_float(make128s(-11259375)) == -11259375.0f);
        STD_INSIST(cast128s_double(make128s(-1)) == -1.0);
        STD_INSIST(cast128s_double(make128s(1)) == 1.0);
        STD_INSIST(cast128s_double(i128(0x8000000000000000, 0)) == -170141183460469231731687303715884105728.0);
    }

    STD_TEST(testFloatToInt) {
        STD_INSIST(same(cast_float_to_u128(0.0), U128_ZERO));
        STD_INSIST(same(cast_float_to_u128(11259375.0), make128(11259375)));
        STD_INSIST(same(cast_float_to_u128(18446744073709551616.0), u128(1, 0)));

        STD_INSIST(same(cast_float_to_u128(432.0 * 1267650600228229401496703205376.0), shl128(make128(432), 100)));

        STD_INSIST(same(cast_float_to_u128(-1.5), U128_ZERO));
        STD_INSIST(same(cast_float_to_u128(1e40), U128_MAX));
        STD_INSIST(same(cast_float_to_u128(__builtin_nan("")), U128_ZERO));

        STD_INSIST(sameS(cast_float_to_i128(0.0), make128s(0)));
        STD_INSIST(sameS(cast_float_to_i128(-11259375.0), make128s(-11259375)));
        STD_INSIST(sameS(cast_float_to_i128(__builtin_nan("")), make128s(0)));
        STD_INSIST(sameS(cast_float_to_i128(1e40), i128(INT64_MAX, UINT64_MAX)));
        STD_INSIST(sameS(cast_float_to_i128(-1e40), i128(0x8000000000000000, 0)));
    }

    STD_TEST(testRoundTripThroughFloat) {
        const uint128_t values[] = {
            make128(0),
            make128(1),
            make128(11259375),
            u128(1, 0),
            shl128(make128(432), 100),
            u128(0x8000000000000000, 0),
        };
        for (const auto& v : values) {
            STD_INSIST(same(cast_float_to_u128(cast128_double(v)), v));
        }
    }
}
