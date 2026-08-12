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

::HIR::TypeRef MonomorphHrlsOnly::get_type(const Span& sp, const ::HIR::GenericRef& ty) const {
    if (ty.group() == 3) {
        ASSERT_BUG(sp, ty.idx() < pp_hrb->m_types.size(), ty << " out of bounds (" << pp_hrb->m_types.size() << ")");
        return pp_hrb->m_types.at(ty.idx());
    }
    return m_types.generic(ty.name, ty.binding);
}
::HIR::ConstGeneric MonomorphHrlsOnly::get_value(const Span& sp, const ::HIR::GenericRef& val) const {
    if (val.group() == 3) {
        ASSERT_BUG(sp, val.idx() < pp_hrb->m_values.size(), val << " out of bounds (" << pp_hrb->m_values.size() << ")");
        return pp_hrb->m_values.at(val.idx()).clone();
    }
    return HIR::ConstGeneric(val);
}
::HIR::LifetimeRef MonomorphHrlsOnly::get_lifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const {
    if (lft_ref.group() == 3) {
        // If the HRL batch does not cover this index, pass the lifetime through rather than abort: not reliably in range for nested binders, and erased before codegen.
        if (lft_ref.idx() >= pp_hrb->m_lifetimes.size()) {
            DEBUG("HRL " << lft_ref << " out of bounds (" << pp_hrb->m_lifetimes.size() << ") - passthrough");
            return ::HIR::LifetimeRef(lft_ref.binding);
        }
        return pp_hrb->m_lifetimes.at(lft_ref.idx());
    }
    return ::HIR::LifetimeRef(lft_ref.binding);
}
