#include "ast_generics.h"

ASTTypeParam::ASTTypeParam(const ASTTypeParam& x)
    : mAttrs(x.mAttrs)
    , mSpan(x.mSpan)
    , mName(x.mName)
    , mDefaultValue(x.mDefaultValue->clone())
{
}

ASTTypeParam::ASTTypeParam(stl::ObjPool& pool, Span sp, ASTAttributeList attrs, RcString name)
    : mAttrs(::std::move(attrs))
    , mSpan(::std::move(sp))
    , mName(::std::move(name))
    , mDefaultValue(mkType(pool, mSpan))
{
}

void ASTTypeParam::setDefault(ASTType* type) {
    assert(mDefaultValue->isWildcard());
    mDefaultValue = ::std::move(type);
}

ASTLifetimeParam::ASTLifetimeParam(Span sp, ASTAttributeList attrs, Ident name)
    : mAttrs(::std::move(attrs))
    , mSpan(::std::move(sp))
    , mName(::std::move(name))
{
}

ASTValueParam::ASTValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* type, ASTExpr val)
    : mAttrs(::std::move(attrs))
    , mSpan(::std::move(sp))
    , mName(::std::move(name))
    , mType(::std::move(type))
    , mDefaultValue(::std::move(val))
{
}

ASTValueParam::ASTValueParam(const ASTValueParam& x)
    : mAttrs(x.mAttrs)
    , mSpan(x.mSpan)
    , mName(x.mName)
    , mType(x.mType->clone())
    , mDefaultValue(x.mDefaultValue ? x.mDefaultValue.clone() : ASTExpr())
{
}

ASTGenericParams::ASTGenericParams() {
}

ASTGenericParams ASTGenericParams::clone() const {
    ASTGenericParams rv;
    rv.mParams.reserve(mParams.size());
    for (const auto& e : mParams) {
        rv.mParams.push_back(e.clone());
    }
    rv.bounds.reserve(bounds.size());
    for (auto& e : bounds) {
        rv.bounds.push_back(e.clone());
    }
    rv.mBareBoundTypes.reserve(mBareBoundTypes.size());
    for (const auto& e : mBareBoundTypes) {
        rv.mBareBoundTypes.push_back(e->clone());
    }
    return rv;
}

void ASTGenericParams::addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd) {
    mParams.push_back(::std::move(gp));
    mParams.back().boundsStart = boundsStart;
    mParams.back().boundsEnd = boundsEnd;
}
