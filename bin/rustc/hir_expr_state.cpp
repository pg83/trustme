#include "hir_expr_state.h"

namespace HIR {

ExprState::ExprState(::HIR::TypeInterner& types, const ::HIR::Module& mod_ptr, ::HIR::SimplePath mod_path)
    : m_types(types)
    , m_mod_path(::std::move(mod_path))
    , m_module(mod_ptr)
    , m_impl_generics(nullptr)
    , m_item_generics(nullptr)
    , m_current_trait_impl(nullptr)
    , stage(Stage::Created) {
}
}
