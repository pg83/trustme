#include "ast_generics.h"

#include "output.h"

using namespace stl;

ASTTypeParam::ASTTypeParam(const ASTTypeParam& x)
    : attrs_(x.attrs_)
    , span_(x.span_)
    , name_(x.name_)
    , defaultValue_(x.defaultValue_->clone())
{
}

ASTTypeParam::ASTTypeParam(ObjPool& pool, Span sp, ASTAttributeList attrs, RcString name)
    : attrs_(std::move(attrs))
    , span_(std::move(sp))
    , name_(std::move(name))
    , defaultValue_(mkType(pool, span_))
{
}

void ASTTypeParam::setDefault(ASTType* type) {
    BUG_ASSERT(defaultValue_->isWildcard());
    defaultValue_ = std::move(type);
}

ASTLifetimeParam::ASTLifetimeParam(Span sp, ASTAttributeList attrs, Ident name)
    : attrs_(std::move(attrs))
    , span_(std::move(sp))
    , name_(std::move(name))
{
}

ASTValueParam::ASTValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* type, ASTExpr val)
    : attrs_(std::move(attrs))
    , span_(std::move(sp))
    , name_(std::move(name))
    , type_(std::move(type))
    , defaultValue_(std::move(val))
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
    rv.bareBoundTypes.grow(bareBoundTypes.length());
    for (const auto& e : bareBoundTypes) {
        rv.bareBoundTypes.pushBack(e->clone());
    }
    return rv;
}

void ASTGenericParams::addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd) {
    params.push_back(std::move(gp));
    params.back().boundsStart = boundsStart;
    params.back().boundsEnd = boundsEnd;
}

template <>
void stl::output<ZeroCopyOutput, ASTTypeParam>(ZeroCopyOutput& out, const ASTTypeParam& value) {
    out << value.name() << StringView(" = ") << value.getDefault();
}

template <>
void stl::output<ZeroCopyOutput, ASTLifetimeParam>(ZeroCopyOutput& out, const ASTLifetimeParam& value) {
    out << StringView("'") << value.name();
}

template <>
void stl::output<ZeroCopyOutput, ASTValueParam>(ZeroCopyOutput& out, const ASTValueParam& value) {
    out << StringView("const ") << value.name() << StringView(": ") << value.type();
}

template <>
void stl::output<ZeroCopyOutput, ASTGenericParams>(ZeroCopyOutput& out, const ASTGenericParams& value) {
    out << StringView("<") << value.params << StringView("> where {") << value.bounds << StringView("}");
}

template <>
void stl::output<ZeroCopyOutput, std::vector<ASTGenericBound>>(ZeroCopyOutput& out, const std::vector<ASTGenericBound>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::vector<GenericParam>>(ZeroCopyOutput& out, const std::vector<GenericParam>& values) {
    outCont(out, values);
}
