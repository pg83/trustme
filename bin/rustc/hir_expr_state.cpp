#include "hir_expr_state.h"

HIRExprState::HIRExprState(HIRTypeInterner& types, HIRModule& modPtr, HIRSimplePath modPath)
    : types(types)
    , modPath(std::move(modPath))
    , module(modPtr)
    , implGenerics(nullptr)
    , itemGenerics(nullptr)
    , currentTraitImpl(nullptr)
    , currentSelfType(nullptr)
    , stage(Stage::Created)
{
}
