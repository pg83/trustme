#include "hir_expr_state.h"

namespace HIR {

ExprState::ExprState(::HIR::TypeInterner& types, const ::HIR::Module& modPtr, ::HIR::SimplePath mod_path)
    : types(types)
    , modPath(::std::move(mod_path))
    , mModule(modPtr)
    , implGenerics(nullptr)
    , itemGenerics(nullptr)
    , currentTraitImpl(nullptr)
    , stage(Stage::Created) {
}
}
