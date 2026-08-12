#include "ast_generics.h"

namespace AST {

TypeParam::TypeParam(const TypeParam& x)
    : m_attrs(x.m_attrs)
    , m_span(x.m_span)
    , m_name(x.m_name)
    , m_default(x.m_default.clone()) {
}
TypeParam::TypeParam(Span sp, ::AST::AttributeList attrs, RcString name)
    : m_attrs(::std::move(attrs))
    , m_span(::std::move(sp))
    , m_name(::std::move(name))
    , m_default(m_span) {
}
void TypeParam::setDefault(TypeRef type) {
    assert(m_default.is_wildcard());
    m_default = ::std::move(type);
}
LifetimeParam::LifetimeParam(Span sp, ::AST::AttributeList attrs, Ident name)
    : m_attrs(::std::move(attrs))
    , m_span(::std::move(sp))
    , m_name(::std::move(name)) {
}
ValueParam::ValueParam(Span sp, ::AST::AttributeList attrs, Ident name, TypeRef type, Expr val)
    : m_attrs(::std::move(attrs))
    , m_span(::std::move(sp))
    , m_name(::std::move(name))
    , m_type(::std::move(type))
    , m_default(::std::move(val)) {
}
ValueParam::ValueParam(const ValueParam& x)
    : m_attrs(x.m_attrs)
    , m_span(x.m_span)
    , m_name(x.m_name)
    , m_type(x.m_type.clone())
    , m_default(x.m_default ? x.m_default.clone() : Expr()) {
}
GenericParams::GenericParams() {
}
GenericParams GenericParams::clone() const {
    GenericParams rv;
    rv.m_params.reserve(m_params.size());
    for (const auto& e : m_params) {
        rv.m_params.push_back(e.clone());
    }
    rv.m_bounds.reserve(m_bounds.size());
    for (auto& e : m_bounds) {
        rv.m_bounds.push_back(e.clone());
    }
    return rv;
}
void GenericParams::add_param(GenericParam gp, size_t bounds_start, size_t bounds_end) {
    m_params.push_back(::std::move(gp));
    m_params.back().bounds_start = bounds_start;
    m_params.back().bounds_end = bounds_end;
}
}
