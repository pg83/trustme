#include "ast_generics.h"

namespace AST {

TypeParam::TypeParam(const TypeParam& x)
    : mAttrs(x.mAttrs)
    , mSpan(x.mSpan)
    , mName(x.mName)
    , defaultValue(x.defaultValue.clone()) {
}
TypeParam::TypeParam(Span sp, ::AST::AttributeList attrs, RcString name)
    : mAttrs(::std::move(attrs))
    , mSpan(::std::move(sp))
    , mName(::std::move(name))
    , defaultValue(mSpan) {
}
void TypeParam::setDefault(TypeRef type) {
    assert(defaultValue.is_wildcard());
    defaultValue = ::std::move(type);
}
LifetimeParam::LifetimeParam(Span sp, ::AST::AttributeList attrs, Ident name)
    : mAttrs(::std::move(attrs))
    , mSpan(::std::move(sp))
    , mName(::std::move(name)) {
}
ValueParam::ValueParam(Span sp, ::AST::AttributeList attrs, Ident name, TypeRef type, Expr val)
    : mAttrs(::std::move(attrs))
    , mSpan(::std::move(sp))
    , mName(::std::move(name))
    , mType(::std::move(type))
    , defaultValue(::std::move(val)) {
}
ValueParam::ValueParam(const ValueParam& x)
    : mAttrs(x.mAttrs)
    , mSpan(x.mSpan)
    , mName(x.mName)
    , mType(x.mType.clone())
    , defaultValue(x.defaultValue ? x.defaultValue.clone() : Expr()) {
}
GenericParams::GenericParams() {
}
GenericParams GenericParams::clone() const {
    GenericParams rv;
    rv.mParams.reserve(mParams.size());
    for (const auto& e : mParams) {
        rv.mParams.push_back(e.clone());
    }
    rv.bounds.reserve(bounds.size());
    for (auto& e : bounds) {
        rv.bounds.push_back(e.clone());
    }
    return rv;
}
void GenericParams::addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd) {
    mParams.push_back(::std::move(gp));
    mParams.back().boundsStart = boundsStart;
    mParams.back().boundsEnd = boundsEnd;
}
}
