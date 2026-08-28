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
        STD_INSIST(unicodeNormaliseNfc("R\xC3\xA9sum\x65\xCC\x81") == "R\xC3\xA9sum\xC3\xA9");
        STD_INSIST(unicodeNormaliseNfc("R\xC3\xA9sum\xC3\xA9") == "R\xC3\xA9sum\xC3\xA9");
        STD_INSIST(!unicodeIsNfc("\x65\xCC\x81"));
        STD_INSIST(unicodeIsNfc("\xC3\xA9"));
    }

    STD_TEST(testHiraganaVoicedMarkComposes) {
        STD_INSIST(unicodeNormaliseNfc("\xE3\x81\x8B\xE3\x82\x99") == "\xE3\x81\x8C");
    }

    STD_TEST(testHangulComposesArithmetically) {
        STD_INSIST(unicodeNormaliseNfc("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB") == "\xED\x95\x9C");
    }

    STD_TEST(testSingletonDecomposes) {
        STD_INSIST(unicodeNormaliseNfc("\xE2\x84\xA6") == "\xCE\xA9");
    }

    STD_TEST(testMarksAreOrderedCanonically) {
        const auto dotAboveFirst = unicodeNormaliseNfc("q\xCC\x87\xCC\xA3");
        const auto dotBelowFirst = unicodeNormaliseNfc("q\xCC\xA3\xCC\x87");
        STD_INSIST(dotAboveFirst == dotBelowFirst);
        STD_INSIST(dotBelowFirst == "q\xCC\xA3\xCC\x87");
    }

    STD_TEST(testExcludedPairStaysApart) {
        STD_INSIST(unicodeNormaliseNfc("\xE0\xA4\x95\xE0\xA4\xBC") == "\xE0\xA4\x95\xE0\xA4\xBC");
        STD_INSIST(unicodeNormaliseNfc("\xE0\xA5\x98") == "\xE0\xA4\x95\xE0\xA4\xBC");
    }
}
