#pragma once

/*
 * Resolving the level a lint reports at for one item
 */

#include "rc_string.h"
#include "settings.h"
#include "thin_vector.h"

class HIRCrate;
class HIRSimplePath;
class ASTAttribute;
class ASTAttributeList;

/* The lint names one `allow`/`warn`/`deny`/`forbid` attribute lists. */
ThinVector<RcString> LintNamesOf(const ASTAttribute& attribute);

/* Records the lint-level attributes of a list into `overrides`; false when it
   has none. */
bool CollectLintLevelAttributes(const ASTAttributeList& attrs, LintLevelOverrides& overrides);

CfgLintLevel ApplyLintLevelOverrides(const Settings& settings, const LintLevelOverrides& overrides, const char* name, CfgLintLevel inherited);

CfgLintLevel LintLevelForModulePath(const Settings& settings, const HIRCrate& crate, const HIRSimplePath& path, const char* name, CfgLintLevel builtin);
