#include "lint_must_use.h"

#include <std/tst/ut.h>
#include <std/mem/obj_pool.h>

using namespace stl;

STD_TEST_SUITE(LintMustUseLevel) {
    STD_TEST(testDefaultIsWarn) {
        auto pool = ObjPool::fromMemory();
        Settings settings{pool.mutPtr()};
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Warn);
    }

    STD_TEST(testExplicitLevelWins) {
        auto pool = ObjPool::fromMemory();
        Settings settings{pool.mutPtr()};
        settings.lintLevels["unused_must_use"] = CfgLintLevel::Deny;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Deny);

        settings.lintLevels["unused_must_use"] = CfgLintLevel::Allow;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Allow);
    }

    STD_TEST(testCapLowersButNeverRaises) {
        auto pool = ObjPool::fromMemory();
        Settings settings{pool.mutPtr()};
        settings.lintLevels["unused_must_use"] = CfgLintLevel::Deny;
        settings.lintCap = CfgLintLevel::Allow;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Allow);

        settings.lintLevels["unused_must_use"] = CfgLintLevel::Allow;
        settings.lintCap = CfgLintLevel::Deny;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Allow);
    }

    STD_TEST(testUnrelatedLintDoesNotMoveIt) {
        auto pool = ObjPool::fromMemory();
        Settings settings{pool.mutPtr()};
        settings.lintLevels["unused_variables"] = CfgLintLevel::Deny;
        STD_INSIST(LintUnusedMustUseLevel(settings) == CfgLintLevel::Warn);
    }
}
