#pragma once

#include "rc_string.h"

#include <map>
#include <string>
#include <vector>

namespace stl {
    class ObjPool;
}

// Trait solver selection. rustc 1.90 uses the new solver for coherence by
// default, while ordinary type checking keeps the legacy solver unless
// explicitly requested.
struct TraitSolverConfig {
    bool coherence = true;
    bool globally = false;
};

struct ASTCrate;
class HIRCrate;
class HIRTypeInterner;
class HIRInherentCache;

// The wiring board: main creates one as the first object of the root pool
// (so it outlives every subsystem holding the reference) and fills the
// fields as the pipeline phases produce them. Rules: a constructor may keep
// the reference but only read fields filled before it; everything else
// dereferences at use time, when the graph is complete. Components read
// dependencies here when needed; they do not cache aliases of these
// canonical fields.
struct WireBoard {
    explicit WireBoard(stl::ObjPool* pool);

    stl::ObjPool* pool = nullptr;
    // The one type interner; created right after the pool, before parsing.
    HIRTypeInterner* types = nullptr;

    // Filled by the Parse phase; stays valid through Expand/Resolve and is
    // dropped conceptually once HIR Lower is done (the pointer survives but
    // later phases must not read it).
    ASTCrate* astCrate = nullptr;
    // Filled by the HIR Lower phase (or by deserialisation in tools); null
    // during the AST-side phases.
    HIRCrate* crate = nullptr;

    // Immutable after main parses the command line.
    TraitSolverConfig solver;

    // Cross-crate index of inherent (non-trait) methods: one instance per
    // compilation. Built by the HIR conversion pipeline over the root crate
    // and every extern crate; read by typeck method resolution.
    HIRInherentCache* inherentMethods = nullptr;

    // Crate-loading configuration; immutable after main parses the command
    // line. `-L` search directories, `--extern name=path` overrides, and
    // names injected without an explicit `extern crate` item.
    ::std::vector<::std::string> crateLoadDirs;
    ::std::map<::std::string, ::std::string> crateOverrides;
    ::std::map<RcString, RcString> implicitCrates;

    // Name of the crate providing core intrinsics ("core" unless building
    // libcore itself) and the name of the crate being compiled. Filled
    // during HIR lowering.
    RcString coreCrate;
    RcString crateName;
};
