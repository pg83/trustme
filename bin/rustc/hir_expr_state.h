#pragma once

#include "hir_hir.h"

namespace HIR {

    class ExprState {
    public:
        ::HIR::TypeInterner& types;
        ::HIR::SimplePath modPath;
        const ::HIR::Module& mModule;

        const ::HIR::GenericParams* mImplGenerics;
        const ::HIR::GenericParams* mItemGenerics;

        // The owner trait implementation is needed when this expression is
        // typechecked or expanded lazily (e.g. while evaluating a const
        // generic).  The normal whole-crate visitors keep this on their
        // traversal stack, but that stack is absent for lazy processing.
        ::HIR::SimplePath mCurrentTraitPath;
        const ::HIR::TraitImpl* currentTraitImpl;

        ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> traits;

        enum class Stage {
            Created,
            ConstEvalRequest,
            ConstEval,
            TypecheckRequest,
            Typecheck,
            PostTypecheck,
            Lifetimes,
            SbcRequest,
            Sbc,
            ExpandRequest,
            Expand,
            MirRequest,
            Mir,
        };
        mutable Stage stage;

        ExprState(::HIR::TypeInterner& types, const ::HIR::Module& modPtr, ::HIR::SimplePath modPath);
    };

}
