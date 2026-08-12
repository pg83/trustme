#include "float128.h"

#include <std/tst/ut.h>

#include <string>
#include <cstdint>
#include <cstring>
#include <sstream>

using namespace stl;

namespace {
    struct BinaryOpVector {
        uint64_t aHi, aLo;
        uint64_t bHi, bLo;
        uint64_t addHi, addLo;
        uint64_t sub_hi, sub_lo;
        uint64_t mul_hi, mul_lo;
        uint64_t divHi, divLo;
        uint64_t mod_hi, mod_lo;
    };

    struct ConvertVector {
        uint64_t hi, lo;
        uint64_t doubleBits;
        uint32_t float_bits;
        int64_t asInt64;
        uint64_t asUint64;
    };

    struct ParseVector {
        const char* text;
        uint64_t hi, lo;
    };

    struct RoundingVector {
        uint64_t hi, lo;
        uint64_t trunc_hi, trunc_lo;
        uint64_t floor_hi, floor_lo;
        uint64_t ceilHi, ceilLo;
        uint64_t round_hi, round_lo;
        uint64_t round_even_hi, round_even_lo;
    };

    struct FormatVector {
        uint64_t hi, lo;
        const char* text;
    };

// Generated into the build tree by the `float128_ut_vectors` node
#include <float128_ut_vectors.inc>

    static bool same_bits(const Float128& value, uint64_t hi, uint64_t lo) {
        return value.bitsHi() == hi && value.bitsLo() == lo;
    }

    static std::string render(const Float128& value) {
        std::ostringstream out;
        out << value;
        return out.str();
    }

    static double doubleFromBits(uint64_t bits) {
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    static uint64_t bitsFromDouble(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }

    static uint32_t bitsFromFloat(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }
}

STD_TEST_SUITE(Float128Vectors) {
    STD_TEST(testBinaryOps) {
        for (const auto& v : binaryOpVectors) {
            const auto a = Float128::from_bits(v.aHi, v.aLo);
            const auto b = Float128::from_bits(v.bHi, v.bLo);
            STD_INSIST(same_bits(a + b, v.addHi, v.addLo));
            STD_INSIST(same_bits(b + a, v.addHi, v.addLo));
            STD_INSIST(same_bits(a - b, v.sub_hi, v.sub_lo));
            STD_INSIST(same_bits(a * b, v.mul_hi, v.mul_lo));
            STD_INSIST(same_bits(b * a, v.mul_hi, v.mul_lo));
            STD_INSIST(same_bits(a / b, v.divHi, v.divLo));
            STD_INSIST(same_bits(Float128::remainder(a, b), v.mod_hi, v.mod_lo));
        }
    }

    STD_TEST(testConversions) {
        for (const auto& v : convertVectors) {
            const auto value = Float128::from_bits(v.hi, v.lo);
            STD_INSIST(bitsFromDouble(static_cast<double>(value)) == v.doubleBits);
            STD_INSIST(bitsFromFloat(static_cast<float>(value)) == v.float_bits);
            STD_INSIST(static_cast<int64_t>(value) == v.asInt64);
            STD_INSIST(static_cast<uint64_t>(value) == v.asUint64);
        }
    }

    STD_TEST(testParse) {
        for (const auto& v : parse_vectors) {
            STD_INSIST(same_bits(Float128::parse_decimal(v.text), v.hi, v.lo));
        }
    }

    STD_TEST(testRounding) {
        for (const auto& v : rounding_vectors) {
            const auto value = Float128::from_bits(v.hi, v.lo);
            STD_INSIST(same_bits(value.trunc(), v.trunc_hi, v.trunc_lo));
            STD_INSIST(same_bits(value.floor(), v.floor_hi, v.floor_lo));
            STD_INSIST(same_bits(value.ceil(), v.ceilHi, v.ceilLo));
            STD_INSIST(same_bits(value.round(), v.round_hi, v.round_lo));
            STD_INSIST(same_bits(value.round_even(), v.round_even_hi, v.round_even_lo));
        }
    }

    STD_TEST(testDefaultFormat) {
        for (const auto& v : format_vectors) {
            STD_INSIST(render(Float128::from_bits(v.hi, v.lo)) == v.text);
        }
    }
}

STD_TEST_SUITE(Float128Specials) {
    STD_TEST(testConstruction) {
        STD_INSIST(same_bits(Float128(), 0, 0));
        STD_INSIST(same_bits(Float128(1.0), 0x3fff'0000'0000'0000ull, 0));
        STD_INSIST(same_bits(Float128(-2.0), 0xc000'0000'0000'0000ull, 0));
        STD_INSIST(same_bits(Float128(0.5), 0x3ffe'0000'0000'0000ull, 0));
        // Subnormal double widens to a normal binary128
        STD_INSIST(same_bits(Float128(doubleFromBits(1)), 0x3bcd'0000'0000'0000ull, 0));
        STD_INSIST(Float128(doubleFromBits(0x7FF8'0000'0000'0000ull)).is_nan());
        STD_INSIST(Float128(doubleFromBits(0x7FF0'0000'0000'0000ull)).is_infinite());
        STD_INSIST(same_bits(Float128::quiet_nan(), 0x7fff'8000'0000'0000ull, 0));
        STD_INSIST(Float128::quiet_nan().is_nan());
        STD_INSIST(Float128::infinity(false).is_infinite());
        STD_INSIST(!Float128::infinity(false).is_nan());
        STD_INSIST(!Float128(1.5).is_nan());
        STD_INSIST(!Float128(1.5).is_infinite());
    }

    STD_TEST(testNanPropagation) {
        const auto nan = Float128::quiet_nan();
        const auto one = Float128(1.0);
        STD_INSIST((nan + one).is_nan());
        STD_INSIST((one - nan).is_nan());
        STD_INSIST((nan * nan).is_nan());
        STD_INSIST((one / nan).is_nan());
        const auto inf = Float128::infinity(false);
        STD_INSIST((inf - inf).is_nan());
        STD_INSIST((inf + (-inf)).is_nan());
        STD_INSIST((Float128() * inf).is_nan());
        STD_INSIST((inf / inf).is_nan());
        STD_INSIST((Float128() / Float128()).is_nan());
        STD_INSIST(Float128::remainder(one, Float128()).is_nan());
        STD_INSIST(Float128::remainder(inf, one).is_nan());
        STD_INSIST(same_bits(Float128::remainder(one, inf), one.bitsHi(), one.bitsLo()));
    }

    STD_TEST(testInfinityArithmetic) {
        const auto inf = Float128::infinity(false);
        const auto one = Float128(1.0);
        STD_INSIST((inf + inf).is_infinite());
        STD_INSIST((inf + one).is_infinite());
        STD_INSIST(same_bits(one / inf, 0, 0));
        STD_INSIST(same_bits(-one / inf, 0x8000'0000'0000'0000ull, 0));
        STD_INSIST((one / Float128()).is_infinite());
        STD_INSIST(same_bits(one / Float128(), inf.bitsHi(), inf.bitsLo()));
        // Overflow rounds to infinity
        const auto max_normal = Float128::from_bits(0x7ffe'ffff'ffff'ffffull, 0xffff'ffff'ffff'ffffull);
        STD_INSIST((max_normal + max_normal).is_infinite());
        STD_INSIST((max_normal * Float128(2.0)).is_infinite());
    }

    STD_TEST(testZeroSigns) {
        STD_INSIST(same_bits(Float128() + Float128(), 0, 0));
        STD_INSIST(same_bits(-Float128() + -Float128(), 0x8000'0000'0000'0000ull, 0));
        STD_INSIST(same_bits(Float128(1.0) - Float128(1.0), 0, 0));
        STD_INSIST(same_bits(Float128(-1.0) * Float128(), 0x8000'0000'0000'0000ull, 0));
        STD_INSIST(Float128() == -Float128());
        STD_INSIST(!(Float128() < -Float128()));
    }

    STD_TEST(testComparisons) {
        const auto one = Float128(1.0);
        const auto two = Float128(2.0);
        const auto nan = Float128::quiet_nan();
        STD_INSIST(one < two);
        STD_INSIST(one <= two);
        STD_INSIST(two > one);
        STD_INSIST(two >= one);
        STD_INSIST(one <= one);
        STD_INSIST(one >= one);
        STD_INSIST(one == one);
        STD_INSIST(one != two);
        STD_INSIST(Float128(-2.0) < Float128(-1.0));
        STD_INSIST(Float128(-1.0) < Float128(0.5));
        // Subnormals order below normals
        STD_INSIST(Float128::from_bits(0, 1) < Float128::from_bits(0x0001'0000'0000'0000ull, 0));
        STD_INSIST(!(nan == nan));
        STD_INSIST(nan != nan);
        STD_INSIST(!(nan < one));
        STD_INSIST(!(nan <= one));
        STD_INSIST(!(one < nan));
        STD_INSIST(!(one >= nan));
        // Infinities order beyond every finite value
        const auto inf = Float128::infinity(false);
        const auto neg_inf = Float128::infinity(true);
        STD_INSIST(one < inf);
        STD_INSIST(one <= inf);
        STD_INSIST(!(inf < one));
        STD_INSIST(inf > one);
        STD_INSIST(neg_inf < one);
        STD_INSIST(neg_inf < Float128(-1.0));
        STD_INSIST(neg_inf < inf);
        STD_INSIST(!(inf < inf));
        STD_INSIST(inf <= inf);
        STD_INSIST(inf == inf);
        STD_INSIST(!(neg_inf > neg_inf));
        STD_INSIST(neg_inf >= neg_inf);
        STD_INSIST(Float128() < inf);
        STD_INSIST(neg_inf < Float128());
        const auto max_normal = Float128::from_bits(0x7ffe'ffff'ffff'ffffull, 0xffff'ffff'ffff'ffffull);
        STD_INSIST(max_normal < inf);
        STD_INSIST(neg_inf < -max_normal);
        STD_INSIST(!(nan < inf));
        STD_INSIST(!(inf < nan));
    }

    STD_TEST(testMinimumMaximum) {
        const auto one = Float128(1.0);
        const auto two = Float128(2.0);
        const auto nan = Float128::quiet_nan();
        STD_INSIST(same_bits(Float128::minimum_number(one, two), one.bitsHi(), one.bitsLo()));
        STD_INSIST(same_bits(Float128::maximum_number(one, two), two.bitsHi(), two.bitsLo()));
        // A single NaN operand loses
        STD_INSIST(same_bits(Float128::minimum_number(nan, two), two.bitsHi(), two.bitsLo()));
        STD_INSIST(same_bits(Float128::maximum_number(one, nan), one.bitsHi(), one.bitsLo()));
        STD_INSIST(Float128::minimum_number(nan, nan).is_nan());
        // Signed zeros are distinguished
        STD_INSIST(same_bits(Float128::minimum_number(Float128(), -Float128()), 0x8000'0000'0000'0000ull, 0));
        STD_INSIST(same_bits(Float128::maximum_number(Float128(), -Float128()), 0, 0));
    }

    STD_TEST(testIntegerSaturation) {
        STD_INSIST(static_cast<int64_t>(Float128::quiet_nan()) == 0);
        STD_INSIST(static_cast<uint64_t>(Float128::quiet_nan()) == 0);
        STD_INSIST(static_cast<int64_t>(Float128::infinity(false)) == INT64_MAX);
        STD_INSIST(static_cast<int64_t>(Float128::infinity(true)) == INT64_MIN);
        STD_INSIST(static_cast<uint64_t>(Float128::infinity(false)) == UINT64_MAX);
        STD_INSIST(static_cast<uint64_t>(Float128::infinity(true)) == 0);
        STD_INSIST(static_cast<uint64_t>(Float128(-1.5)) == 0);
        STD_INSIST(static_cast<int64_t>(Float128(-1.5)) == -1);
        STD_INSIST(static_cast<int64_t>(Float128(9.4e18)) == INT64_MAX);
        STD_INSIST(static_cast<int64_t>(Float128(-9.3e18)) == INT64_MIN);
        // Exactly -2^63 is representable
        STD_INSIST(static_cast<int64_t>(Float128(-9223372036854775808.0)) == INT64_MIN);
    }

    STD_TEST(testNarrowingEdges) {
        // Values beyond double range collapse to infinity / zero
        const auto huge = Float128::from_bits(0x7ffe'0000'0000'0000ull, 0);
        STD_INSIST(bitsFromDouble(static_cast<double>(huge)) == 0x7ff0'0000'0000'0000ull);
        const auto tiny = Float128::from_bits(0x0001'0000'0000'0000ull, 0);
        STD_INSIST(bitsFromDouble(static_cast<double>(tiny)) == 0);
        STD_INSIST(bitsFromDouble(static_cast<double>(-tiny)) == 0x8000'0000'0000'0000ull);
        // A value halfway into double subnormal range
        const auto sub = Float128(doubleFromBits(0x0000'0000'0000'0001ull));
        STD_INSIST(bitsFromDouble(static_cast<double>(sub)) == 1);
        STD_INSIST(Float128(doubleFromBits(0x7ff8'0000'0000'0000ull)).is_nan());
        STD_INSIST(bitsFromDouble(static_cast<double>(Float128::quiet_nan())) == 0x7ff8'0000'0000'0000ull);
        STD_INSIST(bitsFromFloat(static_cast<float>(Float128::infinity(true))) == 0xff80'0000u);
    }

    STD_TEST(testParseSpecialShapes) {
        STD_INSIST(same_bits(Float128::parse_decimal("0."), 0, 0));
        STD_INSIST(same_bits(Float128::parse_decimal("00012.5000"), Float128(12.5).bitsHi(), Float128(12.5).bitsLo()));
        STD_INSIST(same_bits(Float128::parse_decimal("1E3"), Float128(1000.0).bitsHi(), Float128(1000.0).bitsLo()));
        STD_INSIST(same_bits(Float128::parse_decimal("1e+3"), Float128(1000.0).bitsHi(), Float128(1000.0).bitsLo()));
        STD_INSIST(Float128::parse_decimal("1e999999999").is_infinite());
        STD_INSIST(same_bits(Float128::parse_decimal("1e-999999999"), 0, 0));
    }

    STD_TEST(testFormatShapes) {
        STD_INSIST(render(Float128(1.5)) == "1.5");
        STD_INSIST(render(Float128(100.0)) == "100");
        STD_INSIST(render(-Float128()) == "-0");
        STD_INSIST(render(Float128::infinity(false)) == "inf");
        STD_INSIST(render(Float128::infinity(true)) == "-inf");
        STD_INSIST(render(Float128::quiet_nan()) == "nan");
        {
            std::ostringstream out;
            out.precision(18);
            out << std::scientific << Float128(1.0) / Float128(3.0);
            STD_INSIST(out.str() == "3.333333333333333333e-01");
        }
        {
            std::ostringstream out;
            out.precision(10);
            out << std::scientific << Float128(static_cast<double>(1.0f / 3.0f));
            STD_INSIST(out.str() == "3.3333334327e-01");
        }
        {
            std::ostringstream out;
            out.precision(2);
            out << std::fixed << Float128(0.996);
            STD_INSIST(out.str() == "1.00");
        }
        {
            std::ostringstream out;
            out.precision(0);
            out << std::fixed << Float128(0.5);
            STD_INSIST(out.str() == "0");
        }
        {
            std::ostringstream out;
            out.precision(0);
            out << std::fixed << Float128(1.5);
            STD_INSIST(out.str() == "2");
        }
        {
            std::ostringstream out;
            out << std::scientific << Float128();
            STD_INSIST(out.str() == "0.000000e+00");
        }
    }
}
