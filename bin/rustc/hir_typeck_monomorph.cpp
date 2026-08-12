#include "hir_typeck_monomorph.h"

Monomorphiser::Monomorphiser(HIR::TypeInterner& types)
    : m_types(types)
    , consteval_crate(nullptr)
    , consteval_path("") {
}
void Monomorphiser::set_consteval_state(const HIR::Crate& crate, HIR::ItemPath ip) {
    this->consteval_crate = &crate;
    this->consteval_path = ip;
}
const ::HIR::TypeData* Monomorphiser::maybe_monomorph_type(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* ty, bool allow_infer) const {
    if (monomorphise_type_needed(ty)) {
        return tmp = monomorph_type(sp, ty, allow_infer);
    } else {
        return ty;
    }
}
MonomorphiserPP::MonomorphiserPP(HIR::TypeInterner& types): Monomorphiser(types) {}
MonomorphStatePtr::MonomorphStatePtr(HIR::TypeInterner& types)
    : MonomorphiserPP(types)
    , self_ty(nullptr)
    , pp_impl(nullptr)
    , pp_method(nullptr)
    , pp_hrb(nullptr) {
}
MonomorphStatePtr::MonomorphStatePtr(HIR::TypeInterner& types, const ::HIR::TypeData* self_ty, const ::HIR::PathParams* params_i, const ::HIR::PathParams* params_m, const ::HIR::PathParams* params_p, const ::HIR::PathParams* params_h)
    : MonomorphiserPP(types)
    , self_ty(self_ty)
    , pp_impl(params_i)
    , pp_method(params_m)
    //, pp_placeholder(params_p)
    , pp_hrb(params_h) {
}
MonomorphStatePtr::MonomorphStatePtr(MonomorphStatePtr&& x)
    : MonomorphStatePtr(x.type_interner(), x.self_ty, x.pp_impl, x.pp_method, nullptr, x.pp_hrb) {
}
MonomorphStatePtr::MonomorphStatePtr(const MonomorphStatePtr& x)
    : MonomorphStatePtr(x.type_interner(), x.self_ty, x.pp_impl, x.pp_method, nullptr, x.pp_hrb) {
}
MonomorphStatePtr& MonomorphStatePtr::operator=(MonomorphStatePtr&& x) {
    self_ty = x.self_ty;
    pp_impl = x.pp_impl;
    pp_method = x.pp_method;
    pp_hrb = x.pp_hrb;
    return *this;
}
MonomorphHrlsOnly::MonomorphHrlsOnly(HIR::TypeInterner& types, const ::HIR::PathParams& params_h)
    : Monomorphiser(types)
    , pp_hrb(&params_h) {
}
MonomorphState::MonomorphState(HIR::TypeInterner& types)
    : MonomorphiserPP(types)
    , self_ty()
    , pp_impl(nullptr)
    , pp_method(nullptr) {
}
MonomorphState::MonomorphState(MonomorphState&& x)
    : MonomorphState(x.type_interner()) {
    *this = ::std::move(x);
}
MonomorphState& MonomorphState::operator=(MonomorphState&& x) {
    this->self_ty = ::std::move(x.self_ty);
    this->pp_impl = (x.pp_impl == &x.pp_impl_data ? &this->pp_impl_data : x.pp_impl);
    this->pp_impl_data = ::std::move(x.pp_impl_data);
    this->pp_method = x.pp_method;
    return *this;
}
MonomorphState MonomorphState::clone() const {
    MonomorphState rv(this->type_interner());
    rv.self_ty = this->self_ty;
    rv.pp_impl = (this->pp_impl == &this->pp_impl_data ? &rv.pp_impl_data : this->pp_impl);
    rv.pp_impl_data = this->pp_impl_data.clone();
    rv.pp_method = this->pp_method;
    return rv;
}
void MonomorphState::set_impl_params(HIR::PathParams pp) {
    pp_impl = &pp_impl_data;
    pp_impl_data = std::move(pp);
}
