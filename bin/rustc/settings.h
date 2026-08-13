#pragma once

#include "rc_string.h"

#include <map>
#include <string>
#include <vector>

// Trait solver selection. rustc 1.90 uses the new solver for coherence by
// default, while ordinary type checking keeps the legacy solver unless
// explicitly requested.
struct TraitSolverConfig {
    bool coherence = true;
    bool globally = false;
};

// Opaque cfg!() evaluation state (values, flags, --check-cfg expectations,
// lint levels). Defined and created by expand_cfg.cpp.
struct CfgState;

// The one authoritative compilation-settings component, wired on the
// WireBoard as an opaque pointer: consumers that do not read settings see
// only the forward declaration. main fills everything from the command line
// before parsing; the HIR lowering phase fills coreCrate/crateName. Nothing
// here changes after the phase that owns it has run.
struct Settings {
    TraitSolverConfig solver;
    bool overflowChecks = false;

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
};
