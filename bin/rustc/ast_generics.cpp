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
    rv.bareBoundTypes.reserve(bareBoundTypes.size());
    for (const auto& e : bareBoundTypes) {
        rv.bareBoundTypes.push_back(e->clone());
    }
    return rv;
}

void ASTGenericParams::addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd) {
    params.push_back(std::move(gp));
    params.back().boundsStart = boundsStart;
    params.back().boundsEnd = boundsEnd;
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, ASTTypeParam>(ZeroCopyOutput& out, const ASTTypeParam& value) {
        out << value.name() << StringView(" = ") << value.getDefault();
    }

    template <>
    void output<ZeroCopyOutput, ASTLifetimeParam>(ZeroCopyOutput& out, const ASTLifetimeParam& value) {
        out << StringView("'") << value.name();
    }

    template <>
    void output<ZeroCopyOutput, ASTValueParam>(ZeroCopyOutput& out, const ASTValueParam& value) {
        out << StringView("const ") << value.name() << StringView(": ") << value.type();
    }

    template <>
    void output<ZeroCopyOutput, GenericParam>(ZeroCopyOutput& out, const GenericParam& value) {
        switch (value.tag()) {
            case GenericParam::TAG_None:
                out << StringView("/*-*/");
                break;
            case GenericParam::TAG_Lifetime:
                out << value.as_Lifetime();
                break;
            case GenericParam::TAG_Type:
                out << value.as_Type();
                break;
            case GenericParam::TAG_Value:
                out << value.as_Value();
                break;
        }
    }

    template <>
    void output<ZeroCopyOutput, ASTGenericBound>(ZeroCopyOutput& out, const ASTGenericBound& value) {
        switch (value.tag()) {
            case ASTGenericBound::TAG_None:
                out << StringView("/*-*/");
                break;
            case ASTGenericBound::TAG_Lifetime: {
                const auto& inner = value.as_Lifetime();
                out << inner.test << StringView(": ") << inner.bound;
                break;
            }
            case ASTGenericBound::TAG_TypeLifetime: {
                const auto& inner = value.as_TypeLifetime();
                out << inner.type << StringView(": ") << inner.bound;
                break;
            }
            case ASTGenericBound::TAG_IsTrait: {
                const auto& inner = value.as_IsTrait();
                out << inner.outerHrbs << inner.type << StringView(": ");
                if (inner.constness == ASTBoundConstness::Always) {
                    out << StringView("const ");
                } else if (inner.constness == ASTBoundConstness::Maybe) {
                    out << StringView("[const] ");
                }
                out << inner.innerHrbs << inner.trait;
                break;
            }
            case ASTGenericBound::TAG_MaybeTrait: {
                const auto& inner = value.as_MaybeTrait();
                out << inner.type << StringView(": ?") << inner.trait;
                break;
            }
            case ASTGenericBound::TAG_NotTrait: {
                const auto& inner = value.as_NotTrait();
                out << inner.type << StringView(": !") << inner.trait;
                break;
            }
            case ASTGenericBound::TAG_Equality: {
                const auto& inner = value.as_Equality();
                out << inner.type << StringView(" = ") << inner.replacement;
                break;
            }
        }
    }

    template <>
    void output<ZeroCopyOutput, ASTGenericParams>(ZeroCopyOutput& out, const ASTGenericParams& value) {
        out << StringView("<") << value.params << StringView("> where {") << value.bounds << StringView("}");
    }

    template <>
    void output<ZeroCopyOutput, std::vector<ASTGenericBound>>(ZeroCopyOutput& out, const std::vector<ASTGenericBound>& values) {
        outCont(out, values);
    }

    template <>
    void output<ZeroCopyOutput, std::vector<GenericParam>>(ZeroCopyOutput& out, const std::vector<GenericParam>& values) {
        outCont(out, values);
    }
}
