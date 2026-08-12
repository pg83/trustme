#pragma once

#include "hir_hir.h"

namespace HIR {

    class ExprState {
    public:
        ::HIR::TypeInterner& m_types;
        ::HIR::SimplePath m_mod_path;
        const ::HIR::Module& m_module;

        const ::HIR::GenericParams* m_impl_generics;
        const ::HIR::GenericParams* m_item_generics;

        // The owner trait implementation is needed when this expression is
        // typechecked or expanded lazily (e.g. while evaluating a const
        // generic).  The normal whole-crate visitors keep this on their
        // traversal stack, but that stack is absent for lazy processing.
        ::HIR::SimplePath m_current_trait_path;
        const ::HIR::TraitImpl* m_current_trait_impl;

        ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> m_traits;

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

        ExprState(::HIR::TypeInterner& types, const ::HIR::Module& mod_ptr, ::HIR::SimplePath mod_path)
            : m_types(types)
            , m_mod_path(::std::move(mod_path))
            , m_module(mod_ptr)
            , m_impl_generics(nullptr)
            , m_item_generics(nullptr)
            , m_current_trait_impl(nullptr)
            , stage(Stage::Created)
        {
        }
    };

}
