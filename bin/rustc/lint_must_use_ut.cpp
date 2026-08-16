#include "lint_must_use.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(LintMustUseLevel) {
    STD_TEST(testDefaultIsWarn) {
        Settings settings;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Warn);
    }

    STD_TEST(testExplicitLevelWins) {
        Settings settings;
        settings.lintLevels["unused_must_use"] = CfgLintLevel::Deny;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Deny);

        settings.lintLevels["unused_must_use"] = CfgLintLevel::Allow;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Allow);
    }

    STD_TEST(testCapLowersButNeverRaises) {
        Settings settings;
        settings.lintLevels["unused_must_use"] = CfgLintLevel::Deny;
        settings.lintCap = CfgLintLevel::Allow;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Allow);

        settings.lintLevels["unused_must_use"] = CfgLintLevel::Allow;
        settings.lintCap = CfgLintLevel::Deny;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Allow);
    }

    STD_TEST(testUnrelatedLintDoesNotMoveIt) {
        Settings settings;
        settings.lintLevels["unused_variables"] = CfgLintLevel::Deny;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Warn);
    }
}
