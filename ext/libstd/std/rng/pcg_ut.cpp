#include "pcg.h"

#include <std/tst/ut.h>
#include <std/ios/sys.h>

using namespace stl;

STD_TEST_SUITE(RNG) {
    STD_TEST(test32Stability) {
        PCG32 r(42, 54);

        STD_INSIST(r.nextU32() == 0xa15c02b7);
        STD_INSIST(r.nextU32() == 0x7b47f409);
        STD_INSIST(r.nextU32() == 0xba1d3330);
        STD_INSIST(r.nextU32() == 0x83d2f293);
        STD_INSIST(r.nextU32() == 0xbfa4784b);
        STD_INSIST(r.nextU32() == 0xcbed606e);
    }

    STD_TEST(testUniformBiased) {
        i32 cnt[3] = {0};
        PCG32 r(1, 2);

        for (size_t i = 0; i < 2000; ++i) {
            ++cnt[r.uniformBiased(3)];
        }

        STD_INSIST(cnt[0] == 673);
        STD_INSIST(cnt[1] == 655);
        STD_INSIST(cnt[2] == 672);
    }

    STD_TEST(testConstructorWithSingleParam) {
        PCG32 r(12345ULL);

        u32 first = r.nextU32();
        u32 second = r.nextU32();

        STD_INSIST(first != second || first == 0);
    }

    STD_TEST(testUniformBiasedEdgeCases) {
        PCG32 r(0, 1);

        r.uniformBiased(1);

        STD_INSIST(r.uniformBiased(1) == 0);

        for (size_t i = 0; i < 100; ++i) {
            u32 result = r.uniformBiased(1000);
            STD_INSIST(result < 1000);
        }
    }

    STD_TEST(testDeterministicSeeds) {
        PCG32 r1(12345ULL, 67890ULL);
        PCG32 r2(12345ULL, 67890ULL);

        for (int i = 0; i < 10; ++i) {
            STD_INSIST(r1.nextU32() == r2.nextU32());
        }

        for (int i = 0; i < 10; ++i) {
            STD_INSIST(r1.uniformBiased(100) == r2.uniformBiased(100));
        }
    }

    STD_TEST(testDifferentSeeds) {
        PCG32 r1(1ULL, 2ULL);
        PCG32 r2(3ULL, 4ULL);

        bool different = false;
        for (int i = 0; i < 10; ++i) {
            if (r1.nextU32() != r2.nextU32()) {
                different = true;
                break;
            }
        }
        STD_INSIST(different);
    }

    STD_TEST(testLargeNumberOfGenerations) {
        PCG32 r(0x12345678ULL, 0x87654321ULL);

        u32 last = 0;
        bool changed = false;
        for (size_t i = 0; i < 10000; ++i) {
            u32 current = r.nextU32();
            if (i > 0 && current != last) {
                changed = true;
            }
            last = current;
        }

        STD_INSIST(changed);
    }
}
