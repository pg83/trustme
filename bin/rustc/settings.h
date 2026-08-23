#pragma once

#include "rc_string.h"

#include <std/lib/vector.h>
#include <std/sym/i_map.h>

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

/// Lint levels written on one lexical scope. Lint attribute lists are tiny,
/// and names are interned, so a flat vector is cheaper than a separately
/// allocated tree for every function and module.
struct LintLevelOverrides {
    struct Entry {
        RcString name;
        CfgLintLevel level;
        bool isGroup;
    };

    stl::Vector<Entry> entries;

    void set(RcString name, bool isGroup, CfgLintLevel level) {
        for (size_t i = 0; i < entries.length(); ++i) {
            auto& entry = entries.mut(i);
            if (entry.name == name && entry.isGroup == isGroup) {
                entry.level = level;
                return;
            }
        }
        entries.pushBack(Entry{name, level, isGroup});
    }

    const Entry* find(RcString name, bool isGroup) const {
        for (const auto& entry : entries) {
            if (entry.name == name && entry.isGroup == isGroup) {
                return &entry;
            }
        }
        return nullptr;
    }
};

// Opaque cfg!() evaluation state (values, flags, --check-cfg expectations).
// Defined and created by expand_cfg.cpp.
struct CfgState;

// One command-line crate entry.  The same record serves both the exact
// artifact table (`--crate`, `--crate-object`, `--proc-macro`) and the source
// namespace aliases (`--extern`).  Keeping the two roles in one table makes an
// alias cheap while still letting recursive HIR dependencies resolve by their
// unique crate name, without directory scans.
struct CrateOverride {
    RcString name;
    RcString metadataPath;
    RcString objectPath;
    RcString procMacroPath;
    RcString target;
    bool isExtern = false;

    explicit CrateOverride(RcString name)
        : name(name)
    {
    }
};

// The one authoritative compilation-settings component, wired on the
// WireBoard as an opaque pointer: consumers that do not read settings see
// only the forward declaration. main fills everything from the command line
// before parsing; the HIR lowering phase fills coreCrate/crateName. Nothing
// here changes after the phase that owns it has run.
struct Settings {
    explicit Settings(stl::ObjPool* pool)
        : crateOverrides(pool)
    {
    }

    CrateOverride& crateOverride(RcString name) {
        if (auto* entry = crateOverrides.find(name.rawId())) {
            return *entry;
        }
        return *crateOverrides.insert(name.rawId(), name);
    }

    CrateOverride* findCrateOverride(RcString name) const {
        return crateOverrides.find(name.rawId());
    }

    TraitSolverConfig solver;
    bool overflowChecks = false;
    /// Whether the library's UB checks are compiled in (`-Zub-checks`, which
    /// follows debug assertions unless it is given).
    bool ubChecks = false;

    /// Whether `#[link]` attributes contribute native linker directives.
    /// `-Zlink-directives=no` disables only that contribution; the attribute
    /// is still parsed and validated.
    bool linkDirectives = true;

    /// How deep macro expansion may nest (`#![recursion_limit]`). rustc's
    /// default is 128.
    unsigned int recursionLimit = 128;

    /// How much a derived `Debug` prints (`-Zfmt-debug`). `Shallow` prints only
    /// the type or variant name, `None` prints nothing at all.
    enum class FmtDebug {
        Full,
        Shallow,
        None,
    } fmtDebug = FmtDebug::Full;

    // Crate-loading configuration: legacy `-L` search directories, the exact
    // artifact/alias table, and crates injected without an explicit `extern
    // crate` item (loadExternCrate records those as it resolves them).
    ::std::vector<::std::string> crateLoadDirs;
    stl::IntMap<CrateOverride> crateOverrides;
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
