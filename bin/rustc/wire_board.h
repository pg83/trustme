#pragma once

namespace stl {
    class ObjPool;
}

struct ASTCrate;
class HIRCrate;
class HIRTypeInterner;
class HIRInherentCache;
class LangItems;
struct Settings;
struct TargetSpec;
struct NextSolverCrateCache;

// The wiring board: main creates one as the first object of the root pool
// (so it outlives every subsystem holding the reference) and fills the
// fields as the pipeline phases produce them. Rules: a constructor may keep
// the reference but only read fields filled before it; everything else
// dereferences at use time, when the graph is complete. Components read
// dependencies here when needed; they do not cache aliases of these
// canonical fields. Components are opaque pointers — a consumer includes a
// component's header only when it actually uses it.
struct WireBoard {
    explicit WireBoard(stl::ObjPool* pool);

    stl::ObjPool* pool = nullptr;
    // The AST's own pool: everything with parse/expand lifetime. Dropped
    // (physically) right after HIR Lower, together with the AST itself;
    // null from then on.
    stl::ObjPool* astPool = nullptr;
    // The one type interner; created right after the pool, before parsing.
    HIRTypeInterner* types = nullptr;

    // Compilation settings: command-line configuration plus the few values
    // the pipeline derives once (crate names, cfg state). See settings.h.
    Settings* settings = nullptr;

    // The target platform spec (pointer width, endianness, ABI, type layout
    // rules). Derived once from `--target` before parsing; immutable after.
    // See trans_target.h.
    const TargetSpec* target = nullptr;

    // Filled by the Parse phase; stays valid through Expand/Resolve and is
    // dropped conceptually once HIR Lower is done (the pointer survives but
    // later phases must not read it).
    ASTCrate* astCrate = nullptr;
    // Filled by the HIR Lower phase (or by deserialisation in tools); null
    // during the AST-side phases.
    HIRCrate* crate = nullptr;

    // The crate's lang-item paths, resolved once (see lang_items.h). Filled
    // right after `crate`, since that is what defines them.
    LangItems* langItems = nullptr;

    // Cross-crate index of inherent (non-trait) methods: one instance per
    // compilation. Built by the HIR conversion pipeline over the root crate
    // and every extern crate; read by typeck method resolution.
    HIRInherentCache* inherentMethods = nullptr;

    // Crate-lifetime next-solver answer cache for fully concrete goals;
    // created lazily by the first evaluator that stores into it.  See
    // hir_typeck_helpers.h.
    NextSolverCrateCache* solverCache = nullptr;
};
