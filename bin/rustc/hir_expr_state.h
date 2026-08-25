#pragma once

#include "hir_hir.h"

class HIRExprState {
public:
    HIRTypeInterner& types;
    HIRSimplePath modPath;
    const HIRModule& module;

    const HIRGenericParams* implGenerics;
    const HIRGenericParams* itemGenerics;

    // The owner trait implementation is needed when this expression is
    // typechecked or expanded lazily (e.g. while evaluating a const
    // generic).  The normal whole-crate visitors keep this on their
    // traversal stack, but that stack is absent for lazy processing.
    HIRSimplePath currentTraitPath;
    const HIRTraitImpl* currentTraitImpl;
    const HIRTypeData* currentSelfType;

    ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> traits;
    ::std::vector<HIRSimplePath> defineOpaque;

    enum class Stage {
        Created,
        ConstEvalRequest,
        ConstEval,
        TypecheckRequest,
        Typecheck,
        PostTypecheck,
        SbcRequest,
        Sbc,
        ExpandRequest,
        Expand,
        MirRequest,
        Mir,
    };
    mutable Stage stage;

    /// Which generic slots the expression names (rustc-style captures),
    /// computed by one syntactic scan and cached here; the environment of an
    /// unevaluated const may carry unused slots, so evaluability is decided
    /// per used slot. Recomputed when the stage advances past `stage`:
    /// typecheck resolves method calls, refining `unknown`.
    struct Captures {
        bool computed = false;
        Stage stage = Stage::Created;
        // Unresolved method call, nested unevaluated const, out-of-range or
        // non-Impl/Item slot: fall back to whole-environment concreteness.
        bool unknown = false;
        bool usesSelf = false;
        // Bit `idx` set = slot `idx` of that group is named; [0]=Impl, [1]=Item.
        u64 typeMask[2] = {0, 0};
        u64 valueMask[2] = {0, 0};
    };
    mutable Captures captures;

    HIRExprState(HIRTypeInterner& types, const HIRModule& modPtr, HIRSimplePath modPath);
};
