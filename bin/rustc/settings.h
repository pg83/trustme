#pragma once

#include "rc_string.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

// Trait solver selection. rustc 1.90 uses the new solver for coherence by
// default, while ordinary type checking keeps the legacy solver unless
// explicitly requested.
struct TraitSolverConfig {
    bool coherence = true;
    bool globally = false;
};

// Lint reporting level, as set by `-A/-W/-D/-F` and by `#![allow(...)]` and
// friends on the crate.
enum class CfgLintLevel {
    Allow,
    Warn,
    ForceWarn,
    Deny,
    Forbid,
};

// Opaque cfg!() evaluation state (values, flags, --check-cfg expectations).
// Defined and created by expand_cfg.cpp.
struct CfgState;

// The one authoritative compilation-settings component, wired on the
// WireBoard as an opaque pointer: consumers that do not read settings see
// only the forward declaration. main fills everything from the command line
// before parsing; the HIR lowering phase fills coreCrate/crateName. Nothing
// here changes after the phase that owns it has run.
struct Settings {
    TraitSolverConfig solver;
    bool overflowChecks = false;
    /// Whether the library's UB checks are compiled in (`-Zub-checks`, which
    /// follows debug assertions unless it is given).
    bool ubChecks = false;

    // Crate-loading configuration: `-L` search directories, `--extern
    // name=path` overrides, and crates injected without an explicit `extern
    // crate` item (loadExternCrate records those as it resolves them).
    ::std::vector<::std::string> crateLoadDirs;
    ::std::map<::std::string, ::std::string> crateOverrides;
    ::std::map<RcString, RcString> implicitCrates;

    // Name of the crate providing core intrinsics ("core" unless building
    // libcore itself) and the name of the crate being compiled. Filled
    // during HIR lowering.
    RcString coreCrate;
    RcString crateName;

    // cfg!() evaluation state; created by main via CfgCreateState.
    CfgState* cfg = nullptr;

    // Lint levels by name, and the cap applied to every lint (`--cap-lints`).
    // Empty means every lint keeps its built-in level.
    ::std::map<::std::string, CfgLintLevel> lintLevels;
    ::std::optional<CfgLintLevel> lintCap;

    /// Does the lint group `group` contain the lint `name`?
    ///
    /// `warnings` is every lint; the others are the small groups this compiler
    /// actually reports from.
    static bool lintGroupContains(const ::std::string& group, const ::std::string& name) {
        if (group == "warnings") {
            return true;
        }
        if (group == "unused") {
            return name == "unused_must_use" || name == "unused_variables" || name == "unused_imports" || name == "unused_mut" || name == "unused_parens";
        }
        return false;
    }

    /// The level a lint reports at, after its own setting and the cap.
    CfgLintLevel lintLevel(const ::std::string& name, CfgLintLevel builtin) const {
        auto it = lintLevels.find(name);
        auto level = (it != lintLevels.end() ? it->second : builtin);
        if (lintCap && level > *lintCap && level != CfgLintLevel::ForceWarn) {
            level = *lintCap;
        }
        return level;
    }
};
