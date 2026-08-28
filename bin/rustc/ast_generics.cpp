#include "ast_generics.h"

using namespace stl;

ASTTypeParam::ASTTypeParam(const ASTTypeParam& x)
    : attrs_(x.attrs_)
    , span_(x.span_)
    , name_(x.name_)
    , defaultValue_(x.defaultValue_->clone())
{
}

ASTTypeParam::ASTTypeParam(ObjPool& pool, Span sp, ASTAttributeList attrs, RcString name)
    : attrs_(::std::move(attrs))
    , span_(::std::move(sp))
    , name_(::std::move(name))
    , defaultValue_(mkType(pool, span_))
{
}

void ASTTypeParam::setDefault(ASTType* type) {
    assert(defaultValue_->isWildcard());
    defaultValue_ = ::std::move(type);
}

ASTLifetimeParam::ASTLifetimeParam(Span sp, ASTAttributeList attrs, Ident name)
    : attrs_(::std::move(attrs))
    , span_(::std::move(sp))
    , name_(::std::move(name))
{
}

ASTValueParam::ASTValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* type, ASTExpr val)
    : attrs_(::std::move(attrs))
    , span_(::std::move(sp))
    , name_(::std::move(name))
    , type_(::std::move(type))
    , defaultValue_(::std::move(val))
{
}

ASTValueParam::ASTValueParam(const ASTValueParam& x)
    : attrs_(x.attrs_)
    , span_(x.span_)
    , name_(x.name_)
    , type_(x.type_->clone())
    , defaultValue_(x.defaultValue_ ? x.defaultValue_.clone() : ASTExpr())
{
}

ASTGenericParams::ASTGenericParams() {
}

ASTGenericParams ASTGenericParams::clone() const {
    ASTGenericParams rv;
    rv.params.reserve(params.size());
    for (const auto& e : params) {
        rv.params.push_back(e.clone());
    }
    rv.bounds.reserve(bounds.size());
    for (auto& e : bounds) {
        rv.bounds.push_back(e.clone());
    }
    rv.bareBoundTypes.reserve(bareBoundTypes.size());
    for (const auto& e : bareBoundTypes) {
        rv.bareBoundTypes.push_back(e->clone());
    }
    return rv;
}

void ASTGenericParams::addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd) {
    params.push_back(::std::move(gp));
    params.back().boundsStart = boundsStart;
    params.back().boundsEnd = boundsEnd;
}
