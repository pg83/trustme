#pragma once

/*
 * Resolving the level a lint reports at for one item
 */

#include "rc_string.h"
#include "settings.h"

#include <map>

/// The level `name` reports at inside an item carrying these attributes.
///
/// The crate's own setting and `--cap-lints` come first, then the item's:
/// a group it belongs to, then its exact name, which beats the group whichever
/// order the two were written in. `warnings` applies last and only to a lint
/// that is at warn by then, which is what makes `#[deny(warnings)]` next to
/// `#[warn(unsafe_code)]` a denial.
extern CfgLintLevel LintLevelForItem(
    const Settings& settings,
    const ::std::map<RcString, CfgLintLevel>& byName,
    const ::std::map<RcString, CfgLintLevel>& byGroup,
    const char* name,
    CfgLintLevel builtin
);
