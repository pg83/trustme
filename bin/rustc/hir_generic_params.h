#pragma once

#include <string>
#include <vector>
#include <iostream>
#include "hir_generic_ref.h"
#include "hir_path.h"
#include "hir_type_ref.h"

namespace HIR {

    struct TypeParamDef {
        RcString mName;
        ::HIR::TypeRef defaultValue;
        bool isSized;

        Ordering ord(const TypeParamDef& x) const;
    };

    struct LifetimeDef {
        RcString mName;

        Ordering ord(const LifetimeDef& x) const;
    };

    struct ValueParamDef {
        RcString mName;
        ::HIR::TypeRef mType;
        ConstGeneric defaultValue;

        Ordering ord(const ValueParamDef& x) const;
    };

    class GenericParams;

    TAGGED_UNION_EX(
        GenericBound,
        (),
        Lifetime,
        ((Lifetime,
          struct {
              LifetimeRef test;
              LifetimeRef validFor;
          }),
         (TypeLifetime,
          struct {
              ::HIR::TypeRef type;
              LifetimeRef validFor;
          }),
         (TraitBound,
          struct {
              ::std::unique_ptr<::HIR::GenericParams> hrtbs;
              ::HIR::TypeRef type;
              ::HIR::TraitPath trait;
              BoundConstness constness = BoundConstness::Never;
          }) /*,
    (NotTrait, struct {
        ::HIR::TypeRef  type;
        ::HIR::GenricPath    trait;
        })*/
         ,
         (TypeEquality,
          struct {
              ::HIR::TypeRef type;
              ::HIR::TypeRef otherType;
          })),
        (),
        (),
        (GenericBound clone() const; Ordering ord(const GenericBound& x) const;)
    );
    extern ::std::ostream& operator<<(::std::ostream& os, const GenericBound& x);

    class GenericParams {
    public:
        ::std::vector<TypeParamDef> types;
        ::std::vector<LifetimeDef> mLifetimes;
        ::std::vector<ValueParamDef> values;

        ::std::vector<GenericBound> bounds;

        //GenericParams() {}

        GenericParams clone() const;

        bool isEmpty() const;

        bool isGeneric() const;

        /// Create a PathParams instance that doesn't monomorphise at all
        PathParams makeNopParams(TypeInterner& types, unsigned level, bool lifetimesOnly = false) const;

        PathParams makeEmptyParams(bool lifetimesOnly = false) const;

        struct PrintArgs {
            const GenericParams& gp;

            PrintArgs(const GenericParams& gp);

            friend ::std::ostream& operator<<(::std::ostream& os, const PrintArgs& x);
        };

        PrintArgs fmtArgs() const {
            return PrintArgs(*this);
        }

        struct PrintBounds {
            const GenericParams& gp;

            PrintBounds(const GenericParams& gp);

            friend ::std::ostream& operator<<(::std::ostream& os, const PrintBounds& x);
        };

        PrintBounds fmtBounds() const {
            return PrintBounds(*this);
        }

        Ordering ord(const HIR::GenericParams& x) const;
    };

} // namespace HIR
