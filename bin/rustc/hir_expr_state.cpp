#include "hir_expr_state.h"

namespace HIR {

ExprState::ExprState(::HIR::TypeInterner& types, const ::HIR::Module& modPtr, ::HIR::SimplePath modPath)
    : types(types)
    , modPath(::std::move(modPath))
    , mModule(modPtr)
    , mImplGenerics(nullptr)
    , mItemGenerics(nullptr)
    , currentTraitImpl(nullptr)
    , stage(Stage::Created) {
}
}
