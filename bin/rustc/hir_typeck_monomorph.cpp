#include "hir_typeck_monomorph.h"

using namespace stl;

MonomorphStatePtr::MonomorphStatePtr(HIRTypeInterner& types)
    : MonomorphiserPP(types)
    , selfTy(nullptr)
    , ppImpl(nullptr)
    , ppMethod(nullptr)
    , ppHrb(nullptr)
{
}

MonomorphStatePtr::MonomorphStatePtr(HIRTypeInterner& types, const HIRType* selfTy, const HIRPathParams* paramsI, const HIRPathParams* paramsM, const HIRPathParams* paramsP, const HIRPathParams* paramsH)
    : MonomorphiserPP(types)
    , selfTy(selfTy)
    , ppImpl(paramsI)
    , ppMethod(paramsM)
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
    *this = std::move(x);
}

MonomorphState& MonomorphState::operator=(MonomorphState&& x) {
    this->selfTy = std::move(x.selfTy);
    this->ppImpl = (x.ppImpl == &x.ppImplData ? &this->ppImplData : x.ppImpl);
    this->ppImplData = std::move(x.ppImplData);
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

const HIRType* MonomorphiserNop::getType(const Span& sp, const HIRGenericRef& ty) const {
    return types.generic(ty);
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

const HIRType* OpaqueAliasParamMonomorph::getType(const Span& sp, const HIRGenericRef& generic) const {
    auto type = MonomorphiserNop::getType(sp, generic);
    for (size_t i = 0; i < params.types.size(); i++) {
        if (params.types[i] == type) {
            ASSERT_BUG(sp, i < alias.generics.types.size(), StringView("Opaque alias type parameter count mismatch"));
            return types.generic(alias.generics.types[i].name, i);
        }
    }
    return type;
}

HIRConstGeneric OpaqueAliasParamMonomorph::getValue(const Span& sp, const HIRGenericRef& generic) const {
    for (size_t i = 0; i < params.values.size(); i++) {
        const auto* param = params.values[i].opt_Generic();
        if (param && *param == generic) {
            ASSERT_BUG(sp, i < alias.generics.values.size(), StringView("Opaque alias const parameter count mismatch"));
            return HIRGenericRef(alias.generics.values[i].name, i);
        }
    }
    return MonomorphiserNop::getValue(sp, generic);
}

const HIRType* MonomorphStatePtr::getSelfType() const {
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

const HIRType* MonomorphState::getSelfType() const {
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

template <>
void stl::output<ZeroCopyOutput, MonomorphState>(ZeroCopyOutput& os, const MonomorphState& ms) {
    os << StringView("MonomorphState {");
    if (ms.selfTy != nullptr) {
        os << StringView(" self=") << ms.selfTy;
    }
    if (ms.ppImpl) {
        os << StringView(" I=") << *ms.ppImpl;
    }
    if (ms.ppMethod) {
        os << StringView(" M=") << *ms.ppMethod;
    }
    os << StringView(" }");
}
