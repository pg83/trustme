#pragma once

#include "hir_hir.h"
#include "hir_item_path.h"

namespace HIR {
    class TraitImpl;
}

    struct TypeckModuleState {
        const ::HIR::Crate& crate;

        const ::HIR::GenericPath* currentTrait;
        const ::HIR::TraitImpl* currentTraitImpl;
        const ::HIR::GenericParams* mImplGenerics;
        const ::HIR::GenericParams* mItemGenerics;

        ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> traits;
        ::std::vector<HIR::SimplePath> modPaths;

        TypeckModuleState(const ::HIR::Crate& crate);

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

        NullOnDrop<const ::HIR::GenericPath> setCurrentTrait(const ::HIR::GenericPath& p);

        NullOnDrop<const ::HIR::TraitImpl> setCurrentTraitImpl(const ::HIR::TraitImpl& impl);

        NullOnDrop<const ::HIR::GenericParams> setImplGenerics(const ::HIR::GenericParams& gps);

        NullOnDrop<const ::HIR::GenericParams> setItemGenerics(const ::HIR::GenericParams& gps);

        void prepareFromPath(const ::HIR::ItemPath& ip);

        void pushTraits(::HIR::ItemPath p, const ::HIR::Module& mod);

        void popTraits(const ::HIR::Module& mod);
    };

typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> tArgs;
// Needs to mutate the pattern
extern void TypecheckCode(const TypeckModuleState& ms, tArgs& args, const ::HIR::TypeData* resultType, ::HIR::ExprPtr& expr);
extern void TypecheckCodeCS(const TypeckModuleState& ms, tArgs& args, const ::HIR::TypeData* resultType, ::HIR::ExprPtr& expr);
extern void TypecheckCodeSimple(const TypeckModuleState& ms, tArgs& args, const ::HIR::TypeData* resultType, ::HIR::ExprPtr& expr);
