#pragma once

#include "hir_hir.h"
#include "hir_item_path.h"

namespace HIR {
    class TraitImpl;
}

namespace typeck {
    struct ModuleState {
        const ::HIR::Crate& crate;

        const ::HIR::GenericPath* currentTrait;
        const ::HIR::TraitImpl* currentTraitImpl;
        const ::HIR::GenericParams* implGenerics;
        const ::HIR::GenericParams* itemGenerics;

        ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> traits;
        ::std::vector<HIR::SimplePath> modPaths;

        ModuleState(const ::HIR::Crate& crate);

        template <typename T>
        class NullOnDrop {
            T*& ptr;

        public:
            NullOnDrop(T*& ptr)
                : ptr(ptr)
            {
            }

            ~NullOnDrop() {
                ptr = nullptr;
            }
        };

        NullOnDrop<const ::HIR::GenericPath> set_current_trait(const ::HIR::GenericPath& p);

        NullOnDrop<const ::HIR::TraitImpl> set_current_trait_impl(const ::HIR::TraitImpl& impl);

        NullOnDrop<const ::HIR::GenericParams> set_impl_generics(const ::HIR::GenericParams& gps);

        NullOnDrop<const ::HIR::GenericParams> set_item_generics(const ::HIR::GenericParams& gps);

        void prepare_from_path(const ::HIR::ItemPath& ip);

        void push_traits(::HIR::ItemPath p, const ::HIR::Module& mod);

        void pop_traits(const ::HIR::Module& mod);
    };
}

typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> t_args;
// Needs to mutate the pattern
extern void TypecheckCode(const typeck::ModuleState& ms, t_args& args, const ::HIR::TypeData* result_type, ::HIR::ExprPtr& expr);
extern void TypecheckCodeCS(const typeck::ModuleState& ms, t_args& args, const ::HIR::TypeData* result_type, ::HIR::ExprPtr& expr);
extern void TypecheckCodeSimple(const typeck::ModuleState& ms, t_args& args, const ::HIR::TypeData* result_type, ::HIR::ExprPtr& expr);
