#pragma once

#include <string>
#include "ast_attrs.h"
#include "ast_types.h"
#include "ast_path.h"

namespace AST {

    class TypeParam {
        ::AST::AttributeList mAttrs;
        Span mSpan;
        // TODO: use an Ident?
        RcString mName;
        ::TypeRef defaultValue;

    public:
        TypeParam(TypeParam&& x) = default;
        TypeParam& operator=(TypeParam&& x) = default;

        explicit TypeParam(const TypeParam& x);

        TypeParam(Span sp, ::AST::AttributeList attrs, RcString name);

        void setDefault(TypeRef type);

        const ::AST::AttributeList& attrs() const {
            return mAttrs;
        }

        const Span& span() const {
            return mSpan;
        }

        const RcString& name() const {
            return mName;
        }

        const TypeRef& get_default() const {
            return defaultValue;
        }

        TypeRef& get_default() {
            return defaultValue;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const TypeParam& tp);
    };

    class LifetimeParam {
        ::AST::AttributeList mAttrs;
        Span mSpan;
        Ident mName;

    public:
        LifetimeParam(Span sp, ::AST::AttributeList attrs, Ident name);

        LifetimeParam(LifetimeParam&&) = default;
        LifetimeParam& operator=(LifetimeParam&&) = default;
        explicit LifetimeParam(const LifetimeParam&) = default;

        const ::AST::AttributeList& attrs() const {
            return mAttrs;
        }

        const Span& span() const {
            return mSpan;
        }

        const Ident& name() const {
            return mName;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const LifetimeParam& p);
    };

    class ValueParam {
        ::AST::AttributeList mAttrs;
        Span mSpan;
        Ident mName;
        TypeRef mType;
        Expr defaultValue;

    public:
        ValueParam(Span sp, ::AST::AttributeList attrs, Ident name, TypeRef type, Expr val);

        ValueParam(ValueParam&&) = default;
        ValueParam& operator=(ValueParam&&) = default;

        explicit ValueParam(const ValueParam& x);

        const ::AST::AttributeList& attrs() const {
            return mAttrs;
        }

        const Span& span() const {
            return mSpan;
        }

        const Ident& name() const {
            return mName;
        }

        const TypeRef& type() const {
            return mType;
        }

        TypeRef& type() {
            return mType;
        }

        const Expr& default_value() const {
            return defaultValue;
        }

        Expr& default_value() {
            return defaultValue;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const ValueParam& p);
    };

    TAGGED_UNION_EX(
        GenericParam,
        (),
        None,
        ((None, struct {}), (Lifetime, LifetimeParam), (Type, TypeParam), (Value, ValueParam)),
        (, bounds_start(x.bounds_start), bounds_end(x.bounds_end)),
        (bounds_start = x.bounds_start; bounds_end = x.bounds_end;),
        (size_t bounds_start = 0; size_t bounds_end = 0; GenericParam clone() const;

         friend std::ostream & operator<<(std::ostream & os, const GenericParam & x);)
    );

    // HigherRankedBounds is defined in `types.h`

    TAGGED_UNION_EX(
        GenericBound,
        (),
        None,
        ((None, struct {}),
         // Lifetime bound: 'test must be valid for 'bound
         (Lifetime,
          struct {
              LifetimeRef test;
              LifetimeRef bound;
          }),
         // Type lifetime bound
         (TypeLifetime,
          struct {
              TypeRef type;
              LifetimeRef bound;
          }),
         // Standard trait bound: "Type: [for<'a>] Trait"
         (IsTrait,
          struct {
              Span span;
              HigherRankedBounds outer_hrbs;
              TypeRef type;
              HigherRankedBounds inner_hrbs;
              AST::Path trait;
              BoundConstness constness = BoundConstness::Never;
          }),
         // Removed trait bound: "Type: ?Trait"
         (MaybeTrait,
          struct {
              TypeRef type;
              AST::Path trait;
          }),
         // Negative trait bound: "Type: !Trait"
         (NotTrait,
          struct {
              TypeRef type;
              AST::Path trait;
          }),
         // Type equality: "Type = Replacement"
         (Equality,
          struct {
              TypeRef type;
              TypeRef replacement;
          })),

        (, span(x.span)),
        (span = x.span;),
        (public :

         Span span;

         GenericBound clone() const {
             TU_MATCH(GenericBound, ((*this)), (ent), (None, return make_None({});), (Lifetime, return make_Lifetime({ent.test, ent.bound});), (TypeLifetime, return make_TypeLifetime({ent.type.clone(), ent.bound});), (IsTrait, return make_IsTrait({ent.span, ent.outer_hrbs, ent.type.clone(), ent.inner_hrbs, ent.trait, ent.constness});), (MaybeTrait, return make_MaybeTrait({ent.type.clone(), ent.trait});), (NotTrait, return make_NotTrait({ent.type.clone(), ent.trait});), (Equality, return make_Equality({ent.type.clone(), ent.replacement.clone()});))
             return GenericBound();
         })
    );

    ::std::ostream& operator<<(::std::ostream& os, const GenericBound& x);

    class GenericParams {
    public:
        ::std::vector<GenericParam> mParams;
        ::std::vector<GenericBound> bounds;

        GenericParams();

        GenericParams(GenericParams&& x) = default;
        GenericParams& operator=(GenericParams&& x) = default;
        GenericParams(const GenericParams& x) = delete;

        GenericParams clone() const;

        void add_param(GenericParam gp, size_t bounds_start, size_t bounds_end);

        void add_lft_param(LifetimeParam lft) {
            add_param(::std::move(lft), SIZE_MAX, SIZE_MAX);
        }

        void add_lft_param(LifetimeParam lft, size_t bounds_start, size_t bounds_end) {
            add_param(::std::move(lft), bounds_start, bounds_end);
        }

        void add_ty_param(TypeParam param) {
            add_param(::std::move(param), SIZE_MAX, SIZE_MAX);
        }

        void add_ty_param(TypeParam param, size_t bounds_start, size_t bounds_end) {
            add_param(::std::move(param), bounds_start, bounds_end);
        }

        void add_value_param(Span sp, AttributeList attrs, Ident name, TypeRef ty, Expr val) {
            mParams.push_back(ValueParam(mv$(sp), mv$(attrs), mv$(name), mv$(ty), mv$(val)));
        }

        void add_bound(GenericBound bound) {
            bounds.push_back(::std::move(bound));
        }

        int find_name(const char* name) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const GenericParams& tp);
    };

}
