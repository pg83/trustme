#pragma once

#include "hir_hir.h"

class HIRExprState {
public:
    HIRTypeInterner& types;
    HIRSimplePath modPath;
    HIRModule& module;

    const HIRGenericParams* implGenerics;
    const HIRGenericParams* itemGenerics;

    HIRSimplePath currentTraitPath;
    const HIRTraitImpl* currentTraitImpl;
    const HIRType* currentSelfType;

    std::vector<std::pair<const HIRSimplePath*, const HIRTrait*>> traits;
    std::vector<HIRSimplePath> defineOpaque;

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
    Stage stage;

    struct Captures {
        bool computed = false;
        Stage stage = Stage::Created;

        bool unknown = false;
        bool usesSelf = false;

        u64 typeMask[2] = {0, 0};
        u64 valueMask[2] = {0, 0};
    };

    Captures captures;

    /* Creation-order name for an anonymous const-generic body, assigned on first use.
       The name reaches item paths and the names generated from them, so it has to be
       a property of the expression rather than of where it happens to be allocated. */
    u32 anonConstIndex = 0;

    HIRExprState(HIRTypeInterner& types, HIRModule& modPtr, HIRSimplePath modPath);
};
