#include "hir_expr_state.h"


HIRExprState::HIRExprState(HIRTypeInterner& types, const HIRModule& modPtr, HIRSimplePath modPath)
    : types(types)
    , modPath(::std::move(modPath))
    , mModule(modPtr)
    , mImplGenerics(nullptr)
    , mItemGenerics(nullptr)
    , currentTraitImpl(nullptr)
    , stage(Stage::Created) {
}
