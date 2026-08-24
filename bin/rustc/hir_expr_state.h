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

    HIRExprState(HIRTypeInterner& types, const HIRModule& modPtr, HIRSimplePath modPath);
};
