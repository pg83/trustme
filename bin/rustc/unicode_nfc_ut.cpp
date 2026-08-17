#include "unicode_nfc.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(UnicodeNfc) {
    STD_TEST(testAsciiIsUntouched) {
        STD_INSIST(unicodeNormaliseNfc("") == "");
        STD_INSIST(unicodeNormaliseNfc("plain_ascii") == "plain_ascii");
        STD_INSIST(unicodeIsNfc("plain_ascii"));
    }

    STD_TEST(testCombiningMarkComposes) {
        // `e` followed by a combining acute is the precomposed `é`.
        STD_INSIST(unicodeNormaliseNfc("R\xC3\xA9sum\x65\xCC\x81") == "R\xC3\xA9sum\xC3\xA9");
        STD_INSIST(unicodeNormaliseNfc("R\xC3\xA9sum\xC3\xA9") == "R\xC3\xA9sum\xC3\xA9");
        STD_INSIST(!unicodeIsNfc("\x65\xCC\x81"));
        STD_INSIST(unicodeIsNfc("\xC3\xA9"));
    }

    STD_TEST(testHiraganaVoicedMarkComposes) {
        // U+304B KA plus U+3099 COMBINING VOICED SOUND MARK is U+304C GA.
        STD_INSIST(unicodeNormaliseNfc("\xE3\x81\x8B\xE3\x82\x99") == "\xE3\x81\x8C");
    }

    STD_TEST(testHangulComposesArithmetically) {
        // The jamo of U+D55C are composed without a table.
        STD_INSIST(unicodeNormaliseNfc("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB") == "\xED\x95\x9C");
    }

    STD_TEST(testSingletonDecomposes) {
        // U+2126 OHM SIGN decomposes to U+03A9 GREEK CAPITAL OMEGA and stays there.
        STD_INSIST(unicodeNormaliseNfc("\xE2\x84\xA6") == "\xCE\xA9");
    }

    STD_TEST(testMarksAreOrderedCanonically) {
        // Two marks of different classes normalise the same either way round.
        const auto dotAboveFirst = unicodeNormaliseNfc("q\xCC\x87\xCC\xA3");
        const auto dotBelowFirst = unicodeNormaliseNfc("q\xCC\xA3\xCC\x87");
        STD_INSIST(dotAboveFirst == dotBelowFirst);
        // The lower class comes first: U+0323 (220) before U+0307 (230).
        STD_INSIST(dotBelowFirst == "q\xCC\xA3\xCC\x87");
    }

    STD_TEST(testExcludedPairStaysApart) {
        // U+0958 has a canonical decomposition but is a composition exclusion,
        // so its parts must not be joined back.
        STD_INSIST(unicodeNormaliseNfc("\xE0\xA4\x95\xE0\xA4\xBC") == "\xE0\xA4\x95\xE0\xA4\xBC");
        STD_INSIST(unicodeNormaliseNfc("\xE0\xA5\x98") == "\xE0\xA4\x95\xE0\xA4\xBC");
    }
}

