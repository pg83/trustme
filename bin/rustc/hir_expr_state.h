#pragma once

#include "hir_hir.h"

class HIRExprState {
public:
    HIRTypeInterner& types;
    HIRSimplePath modPath;
    const HIRModule& module;

    const HIRGenericParams* implGenerics;
    const HIRGenericParams* itemGenerics;

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

    struct Captures {
        bool computed = false;
        Stage stage = Stage::Created;

        bool unknown = false;
        bool usesSelf = false;

        u64 typeMask[2] = {0, 0};
        u64 valueMask[2] = {0, 0};
    };

    mutable Captures captures;

    HIRExprState(HIRTypeInterner& types, const HIRModule& modPtr, HIRSimplePath modPath);
};
