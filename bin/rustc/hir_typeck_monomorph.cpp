#include "hir_typeck_monomorph.h"

Monomorphiser::Monomorphiser(HIRTypeInterner& types)
    : types(types)
    , constevalWb(nullptr)
    , constevalPath("")
{
}

void Monomorphiser::setConstevalState(const WireBoard& wb, HIRItemPath ip) {
    this->constevalWb = &wb;
    this->constevalPath = ip;
}

const HIRTypeData* Monomorphiser::maybeMonomorphType(const Span& sp, HIRTypeRef& tmp, const HIRTypeData* ty, bool allowInfer) const {
    if (monomorphiseTypeNeeded(ty)) {
        return tmp = monomorphType(sp, ty, allowInfer);
    } else {
        return ty;
    }
}

MonomorphiserPP::MonomorphiserPP(HIRTypeInterner& types)
    : Monomorphiser(types)
{
}

MonomorphStatePtr::MonomorphStatePtr(HIRTypeInterner& types)
    : MonomorphiserPP(types)
    , selfTy(nullptr)
    , ppImpl(nullptr)
    , ppMethod(nullptr)
    , ppHrb(nullptr)
{
}

MonomorphStatePtr::MonomorphStatePtr(HIRTypeInterner& types, const HIRTypeData* selfTy, const HIRPathParams* paramsI, const HIRPathParams* paramsM, const HIRPathParams* paramsP, const HIRPathParams* paramsH)
    : MonomorphiserPP(types)
    , selfTy(selfTy)
    , ppImpl(paramsI)
    , ppMethod(paramsM)
    //, pp_placeholder(params_p)
    , ppHrb(paramsH)
{
}

MonomorphStatePtr::MonomorphStatePtr(MonomorphStatePtr&& x)
    : MonomorphStatePtr(x.typeInterner(), x.selfTy, x.ppImpl, x.ppMethod, nullptr, x.ppHrb)
{
}

MonomorphStatePtr::MonomorphStatePtr(const MonomorphStatePtr& x)
    : MonomorphStatePtr(x.typeInterner(), x.selfTy, x.ppImpl, x.ppMethod, nullptr, x.ppHrb)
{
}

MonomorphStatePtr& MonomorphStatePtr::operator=(MonomorphStatePtr&& x) {
    selfTy = x.selfTy;
    ppImpl = x.ppImpl;
    ppMethod = x.ppMethod;
    ppHrb = x.ppHrb;
    return *this;
}

MonomorphState::MonomorphState(HIRTypeInterner& types)
    : MonomorphiserPP(types)
    , selfTy()
    , ppImpl(nullptr)
    , ppMethod(nullptr)
{
}

MonomorphState::MonomorphState(MonomorphState&& x)
    : MonomorphState(x.typeInterner())
{
    *this = ::std::move(x);
}

MonomorphState& MonomorphState::operator=(MonomorphState&& x) {
    this->selfTy = ::std::move(x.selfTy);
    this->ppImpl = (x.ppImpl == &x.ppImplData ? &this->ppImplData : x.ppImpl);
    this->ppImplData = ::std::move(x.ppImplData);
    this->ppMethod = x.ppMethod;
    return *this;
}

MonomorphState MonomorphState::clone() const {
    MonomorphState rv(this->typeInterner());
    rv.selfTy = this->selfTy;
    rv.ppImpl = (this->ppImpl == &this->ppImplData ? &rv.ppImplData : this->ppImpl);
    rv.ppImplData = this->ppImplData.clone();
    rv.ppMethod = this->ppMethod;
    return rv;
}

void MonomorphState::setImplParams(HIRPathParams pp) {
    ppImpl = &ppImplData;
    ppImplData = std::move(pp);
}

HIRTypeRef MonomorphiserNop::getType(const Span& sp, const HIRGenericRef& ty) const {
    return types.generic(ty.name, ty.binding);
}

HIRConstGeneric MonomorphiserNop::getValue(const Span& sp, const HIRGenericRef& val) const {
    return HIRConstGeneric(val);
}

OpaqueAliasParamMonomorph::OpaqueAliasParamMonomorph(HIRTypeInterner& types, const HIRTypeDataErasedTypeAliasInner& alias, const HIRPathParams& params)
    : MonomorphiserNop(types)
    , alias(alias)
    , params(params)
{
}

HIRTypeRef OpaqueAliasParamMonomorph::getType(const Span& sp, const HIRGenericRef& generic) const {
    auto type = MonomorphiserNop::getType(sp, generic);
    for (size_t i = 0; i < params.types.size(); i++) {
        if (params.types[i] == type) {
            ASSERT_BUG(sp, i < alias.generics.types.size(), "Opaque alias type parameter count mismatch");
            return types.generic(alias.generics.types[i].name, i);
        }
    }
    return type;
}

HIRConstGeneric OpaqueAliasParamMonomorph::getValue(const Span& sp, const HIRGenericRef& generic) const {
    for (size_t i = 0; i < params.values.size(); i++) {
        const auto* param = params.values[i].opt_Generic();
        if (param && *param == generic) {
            ASSERT_BUG(sp, i < alias.generics.values.size(), "Opaque alias const parameter count mismatch");
            return HIRGenericRef(alias.generics.values[i].name, i);
        }
    }
    return MonomorphiserNop::getValue(sp, generic);
}

const HIRTypeData* MonomorphStatePtr::getSelfType() const {
    return selfTy;
}

const HIRPathParams* MonomorphStatePtr::getImplParams() const {
    return ppImpl;
}

const HIRPathParams* MonomorphStatePtr::getMethodParams() const {
    return ppMethod;
}

const HIRPathParams* MonomorphStatePtr::getHrbParams() const {
    return ppHrb;
}

const HIRTypeData* MonomorphState::getSelfType() const {
    return selfTy;
}

const HIRPathParams* MonomorphState::getImplParams() const {
    return ppImpl;
}

const HIRPathParams* MonomorphState::getMethodParams() const {
    return ppMethod;
}

const HIRPathParams* MonomorphState::getHrbParams() const {
    return nullptr;
}
