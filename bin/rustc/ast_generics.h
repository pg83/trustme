#pragma once

#include "ast_path.h"
#include "ast_attrs.h"
#include "ast_types.h"

#include <string>

class ASTTypeParam {
    ASTAttributeList attrs_;
    Span span_;
    // TODO: use an Ident?
    RcString name_;
    ::ASTType* defaultValue_;

public:
    ASTTypeParam(ASTTypeParam&& x) = default;
    ASTTypeParam& operator=(ASTTypeParam&& x) = default;

    explicit ASTTypeParam(const ASTTypeParam& x);

    ASTTypeParam(stl::ObjPool& pool, Span sp, ASTAttributeList attrs, RcString name);

    void setDefault(ASTType* type);

    const ASTAttributeList& attrs() const {
        return attrs_;
    }

    const Span& span() const {
        return span_;
    }

    const RcString& name() const {
        return name_;
    }

    ASTType* getDefault() const {
        return defaultValue_;
    }

    ASTType*& getDefault() {
        return defaultValue_;
    }

};

class ASTLifetimeParam {
    ASTAttributeList attrs_;
    Span span_;
    Ident name_;

public:
    ASTLifetimeParam(Span sp, ASTAttributeList attrs, Ident name);

    ASTLifetimeParam(ASTLifetimeParam&&) = default;
    ASTLifetimeParam& operator=(ASTLifetimeParam&&) = default;
    explicit ASTLifetimeParam(const ASTLifetimeParam&) = default;

    const ASTAttributeList& attrs() const {
        return attrs_;
    }

    const Span& span() const {
        return span_;
    }

    const Ident& name() const {
        return name_;
    }

};

class ASTValueParam {
    ASTAttributeList attrs_;
    Span span_;
    Ident name_;
    ASTType* type_;
    ASTExpr defaultValue_;

public:
    ASTValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* type, ASTExpr val);

    ASTValueParam(ASTValueParam&&) = default;
    ASTValueParam& operator=(ASTValueParam&&) = default;

    explicit ASTValueParam(const ASTValueParam& x);

    const ASTAttributeList& attrs() const {
        return attrs_;
    }

    const Span& span() const {
        return span_;
    }

    const Ident& name() const {
        return name_;
    }

    ASTType* type() const {
        return type_;
    }

    ASTType*& type() {
        return type_;
    }

    const ASTExpr& defaultValue() const {
        return defaultValue_;
    }

    ASTExpr& defaultValue() {
        return defaultValue_;
    }

};

#include "ast_generics_tu.h"

class ASTGenericParams {
public:
    std::vector<GenericParam> params;
    std::vector<ASTGenericBound> bounds;

    std::vector<ASTType*> bareBoundTypes;

    ASTGenericParams();

    ASTGenericParams(ASTGenericParams&& x) = default;
    ASTGenericParams& operator=(ASTGenericParams&& x) = default;
    ASTGenericParams(const ASTGenericParams& x) = delete;

    ASTGenericParams clone() const;

    void addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd);

    void addLftParam(ASTLifetimeParam lft) {
        addParam(std::move(lft), SIZE_MAX, SIZE_MAX);
    }

    void addLftParam(ASTLifetimeParam lft, size_t boundsStart, size_t boundsEnd) {
        addParam(std::move(lft), boundsStart, boundsEnd);
    }

    void addTyParam(ASTTypeParam param) {
        addParam(std::move(param), SIZE_MAX, SIZE_MAX);
    }

    void addTyParam(ASTTypeParam param, size_t boundsStart, size_t boundsEnd) {
        addParam(std::move(param), boundsStart, boundsEnd);
    }

    void addValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* ty, ASTExpr val) {
        params.push_back(ASTValueParam(mv$(sp), mv$(attrs), mv$(name), mv$(ty), mv$(val)));
    }

    void addBound(ASTGenericBound bound) {
        bounds.push_back(std::move(bound));
    }

    int findName(const char* name) const;

};
