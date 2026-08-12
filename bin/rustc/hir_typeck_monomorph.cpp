#include "hir_typeck_monomorph.h"

Monomorphiser::Monomorphiser(HIR::TypeInterner& types)
    : types(types)
    , constevalCrate(nullptr)
    , constevalPath("") {
}
void Monomorphiser::setConstevalState(const HIR::Crate& crate, HIR::ItemPath ip) {
    this->constevalCrate = &crate;
    this->constevalPath = ip;
}
const ::HIR::TypeData* Monomorphiser::maybeMonomorphType(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* ty, bool allowInfer) const {
    if (monomorphiseTypeNeeded(ty)) {
        return tmp = monomorphType(sp, ty, allowInfer);
    } else {
        return ty;
    }
}
MonomorphiserPP::MonomorphiserPP(HIR::TypeInterner& types): Monomorphiser(types) {}
MonomorphStatePtr::MonomorphStatePtr(HIR::TypeInterner& types)
    : MonomorphiserPP(types)
    , self_ty(nullptr)
    , ppImpl(nullptr)
    , ppMethod(nullptr)
    , ppHrb(nullptr) {
}
MonomorphStatePtr::MonomorphStatePtr(HIR::TypeInterner& types, const ::HIR::TypeData* self_ty, const ::HIR::PathParams* paramsI, const ::HIR::PathParams* paramsM, const ::HIR::PathParams* paramsP, const ::HIR::PathParams* paramsH)
    : MonomorphiserPP(types)
    , self_ty(self_ty)
    , ppImpl(paramsI)
    , ppMethod(paramsM)
    //, pp_placeholder(params_p)
    , ppHrb(paramsH) {
}
MonomorphStatePtr::MonomorphStatePtr(MonomorphStatePtr&& x)
    : MonomorphStatePtr(x.type_interner(), x.self_ty, x.ppImpl, x.ppMethod, nullptr, x.ppHrb) {
}
MonomorphStatePtr::MonomorphStatePtr(const MonomorphStatePtr& x)
    : MonomorphStatePtr(x.type_interner(), x.self_ty, x.ppImpl, x.ppMethod, nullptr, x.ppHrb) {
}
MonomorphStatePtr& MonomorphStatePtr::operator=(MonomorphStatePtr&& x) {
    self_ty = x.self_ty;
    ppImpl = x.ppImpl;
    ppMethod = x.ppMethod;
    ppHrb = x.ppHrb;
    return *this;
}
MonomorphHrlsOnly::MonomorphHrlsOnly(HIR::TypeInterner& types, const ::HIR::PathParams& paramsH)
    : Monomorphiser(types)
    , ppHrb(&paramsH) {
}
MonomorphState::MonomorphState(HIR::TypeInterner& types)
    : MonomorphiserPP(types)
    , self_ty()
    , ppImpl(nullptr)
    , ppMethod(nullptr) {
}
MonomorphState::MonomorphState(MonomorphState&& x)
    : MonomorphState(x.type_interner()) {
    *this = ::std::move(x);
}
MonomorphState& MonomorphState::operator=(MonomorphState&& x) {
    this->self_ty = ::std::move(x.self_ty);
    this->ppImpl = (x.ppImpl == &x.ppImplData ? &this->ppImplData : x.ppImpl);
    this->ppImplData = ::std::move(x.ppImplData);
    this->ppMethod = x.ppMethod;
    return *this;
}
MonomorphState MonomorphState::clone() const {
    MonomorphState rv(this->type_interner());
    rv.self_ty = this->self_ty;
    rv.ppImpl = (this->ppImpl == &this->ppImplData ? &rv.ppImplData : this->ppImpl);
    rv.ppImplData = this->ppImplData.clone();
    rv.ppMethod = this->ppMethod;
    return rv;
}
void MonomorphState::setImplParams(HIR::PathParams pp) {
    ppImpl = &ppImplData;
    ppImplData = std::move(pp);
}

::HIR::TypeRef MonomorphHrlsOnly::getType(const Span& sp, const ::HIR::GenericRef& ty) const {
    if (ty.group() == 3) {
        ASSERT_BUG(sp, ty.idx() < ppHrb->types.size(), ty << " out of bounds (" << ppHrb->types.size() << ")");
        return ppHrb->types.at(ty.idx());
    }
    return types.generic(ty.name, ty.binding);
}
::HIR::ConstGeneric MonomorphHrlsOnly::getValue(const Span& sp, const ::HIR::GenericRef& val) const {
    if (val.group() == 3) {
        ASSERT_BUG(sp, val.idx() < ppHrb->values.size(), val << " out of bounds (" << ppHrb->values.size() << ")");
        return ppHrb->values.at(val.idx()).clone();
    }
    return HIR::ConstGeneric(val);
}
::HIR::LifetimeRef MonomorphHrlsOnly::getLifetime(const Span& sp, const ::HIR::GenericRef& lftRef) const {
    if (lftRef.group() == 3) {
        // If the HRL batch does not cover this index, pass the lifetime through rather than abort: not reliably in range for nested binders, and erased before codegen.
        if (lftRef.idx() >= ppHrb->mLifetimes.size()) {
            DEBUG("HRL " << lftRef << " out of bounds (" << ppHrb->mLifetimes.size() << ") - passthrough");
            return ::HIR::LifetimeRef(lftRef.binding);
        }
        return ppHrb->mLifetimes.at(lftRef.idx());
    }
    return ::HIR::LifetimeRef(lftRef.binding);
}
