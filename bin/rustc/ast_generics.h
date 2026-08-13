#pragma once

#include "ast_path.h"
#include "ast_attrs.h"
#include "ast_types.h"

#include <string>

class ASTTypeParam {
    ASTAttributeList mAttrs;
    Span mSpan;
    // TODO: use an Ident?
    RcString mName;
    ::ASTType* mDefaultValue;

public:
    ASTTypeParam(ASTTypeParam&& x) = default;
    ASTTypeParam& operator=(ASTTypeParam&& x) = default;

    explicit ASTTypeParam(const ASTTypeParam& x);

    ASTTypeParam(stl::ObjPool& pool, Span sp, ASTAttributeList attrs, RcString name);

    void setDefault(ASTType* type);

    const ASTAttributeList& attrs() const {
        return mAttrs;
    }

    const Span& span() const {
        return mSpan;
    }

    const RcString& name() const {
        return mName;
    }

    ASTType* getDefault() const {
        return mDefaultValue;
    }

    ASTType*& getDefault() {
        return mDefaultValue;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTTypeParam& tp);
};

class ASTLifetimeParam {
    ASTAttributeList mAttrs;
    Span mSpan;
    Ident mName;

public:
    ASTLifetimeParam(Span sp, ASTAttributeList attrs, Ident name);

    ASTLifetimeParam(ASTLifetimeParam&&) = default;
    ASTLifetimeParam& operator=(ASTLifetimeParam&&) = default;
    explicit ASTLifetimeParam(const ASTLifetimeParam&) = default;

    const ASTAttributeList& attrs() const {
        return mAttrs;
    }

    const Span& span() const {
        return mSpan;
    }

    const Ident& name() const {
        return mName;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTLifetimeParam& p);
};

class ASTValueParam {
    ASTAttributeList mAttrs;
    Span mSpan;
    Ident mName;
    ASTType* mType;
    ASTExpr mDefaultValue;

public:
    ASTValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* type, ASTExpr val);

    ASTValueParam(ASTValueParam&&) = default;
    ASTValueParam& operator=(ASTValueParam&&) = default;

    explicit ASTValueParam(const ASTValueParam& x);

    const ASTAttributeList& attrs() const {
        return mAttrs;
    }

    const Span& span() const {
        return mSpan;
    }

    const Ident& name() const {
        return mName;
    }

    ASTType* type() const {
        return mType;
    }

    ASTType*& type() {
        return mType;
    }

    const ASTExpr& defaultValue() const {
        return mDefaultValue;
    }

    ASTExpr& defaultValue() {
        return mDefaultValue;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTValueParam& p);
};

TAGGED_UNION_EX(
    GenericParam,
    (),
    None,
    ((None, struct {}), (Lifetime, ASTLifetimeParam), (Type, ASTTypeParam), (Value, ASTValueParam)),
    (, boundsStart(x.boundsStart), boundsEnd(x.boundsEnd)),
    (boundsStart = x.boundsStart; boundsEnd = x.boundsEnd;),
    (size_t boundsStart = 0; size_t boundsEnd = 0; GenericParam clone() const;

     friend std::ostream & operator<<(std::ostream & os, const GenericParam & x);)
);

// HigherRankedBounds is defined in `types.h`

TAGGED_UNION_EX(
    ASTGenericBound,
    (),
    None,
    ((None, struct {}),
     // Lifetime bound: 'test must be valid for 'bound
     (Lifetime,
      struct {
          ASTLifetimeRef test;
          ASTLifetimeRef bound;
      }),
     // Type lifetime bound
     (TypeLifetime,
      struct {
          ASTType* type;
          ASTLifetimeRef bound;
      }),
     // Standard trait bound: "Type: [for<'a>] Trait"
     (IsTrait,
      struct {
          Span span;
          ASTHigherRankedBounds outerHrbs;
          ASTType* type;
          ASTHigherRankedBounds innerHrbs;
          ASTPath trait;
          ASTBoundConstness constness = ASTBoundConstness::Never;
      }),
     // Removed trait bound: "Type: ?Trait"
     (MaybeTrait,
      struct {
          ASTType* type;
          ASTPath trait;
      }),
     // Negative trait bound: "Type: !Trait"
     (NotTrait,
      struct {
          ASTType* type;
          ASTPath trait;
      }),
     // Type equality: "Type = Replacement"
     (Equality,
      struct {
          ASTType* type;
          ASTType* replacement;
      })),

    (, span(x.span)),
    (span = x.span;),
    (public :

     Span span;

     ASTGenericBound clone() const {
         TU_MATCH(ASTGenericBound, ((*this)), (ent), (None, return make_None({});), (Lifetime, return make_Lifetime({ent.test, ent.bound});), (TypeLifetime, return make_TypeLifetime({ent.type->clone(), ent.bound});), (IsTrait, return make_IsTrait({ent.span, ent.outerHrbs, ent.type->clone(), ent.innerHrbs, ent.trait, ent.constness});), (MaybeTrait, return make_MaybeTrait({ent.type->clone(), ent.trait});), (NotTrait, return make_NotTrait({ent.type->clone(), ent.trait});), (Equality, return make_Equality({ent.type->clone(), ent.replacement->clone()});))
         return ASTGenericBound();
     })
);

::std::ostream& operator<<(::std::ostream& os, const ASTGenericBound& x);

class ASTGenericParams {
public:
    ::std::vector<GenericParam> mParams;
    ::std::vector<ASTGenericBound> bounds;
    // Types that appear in a `where` clause with an empty bound list (`T:`).
    // These impose no constraint, but must still be expanded/resolved so that
    // any side effects they carry (e.g. anon-const blocks with nested items)
    // are processed consistently with how lowering later visits them.
    ::std::vector<ASTType*> mBareBoundTypes;

    ASTGenericParams();

    ASTGenericParams(ASTGenericParams&& x) = default;
    ASTGenericParams& operator=(ASTGenericParams&& x) = default;
    ASTGenericParams(const ASTGenericParams& x) = delete;

    ASTGenericParams clone() const;

    void addParam(GenericParam gp, size_t boundsStart, size_t boundsEnd);

    void addLftParam(ASTLifetimeParam lft) {
        addParam(::std::move(lft), SIZE_MAX, SIZE_MAX);
    }

    void addLftParam(ASTLifetimeParam lft, size_t boundsStart, size_t boundsEnd) {
        addParam(::std::move(lft), boundsStart, boundsEnd);
    }

    void addTyParam(ASTTypeParam param) {
        addParam(::std::move(param), SIZE_MAX, SIZE_MAX);
    }

    void addTyParam(ASTTypeParam param, size_t boundsStart, size_t boundsEnd) {
        addParam(::std::move(param), boundsStart, boundsEnd);
    }

    void addValueParam(Span sp, ASTAttributeList attrs, Ident name, ASTType* ty, ASTExpr val) {
        mParams.push_back(ASTValueParam(mv$(sp), mv$(attrs), mv$(name), mv$(ty), mv$(val)));
    }

    void addBound(ASTGenericBound bound) {
        bounds.push_back(::std::move(bound));
    }

    int findName(const char* name) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTGenericParams& tp);
};
