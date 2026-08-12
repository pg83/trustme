#pragma once

#include "hir_path.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"

#include <string>
#include <vector>
#include <iostream>

struct HIRTypeParamDef {
    RcString mName;
    HIRTypeRef defaultValue;
    bool isSized;

    Ordering ord(const HIRTypeParamDef& x) const;
};

struct HIRLifetimeDef {
    RcString mName;

    Ordering ord(const HIRLifetimeDef& x) const;
};

struct HIRValueParamDef {
    RcString mName;
    HIRTypeRef mType;
    HIRConstGeneric defaultValue;

    Ordering ord(const HIRValueParamDef& x) const;
};

class HIRGenericParams;

TAGGED_UNION_EX(
    HIRGenericBound,
    (),
    Lifetime,
    ((Lifetime,
      struct {
          HIRLifetimeRef test;
          HIRLifetimeRef validFor;
      }),
     (TypeLifetime,
      struct {
          HIRTypeRef type;
          HIRLifetimeRef validFor;
      }),
     (TraitBound,
      struct {
          ::std::unique_ptr<HIRGenericParams> hrtbs;
          HIRTypeRef type;
          HIRTraitPath trait;
          HIRBoundConstness constness = HIRBoundConstness::Never;
      }) /*,
    (NotTrait, struct {
        ::HIR::TypeRef  type;
        ::HIR::GenricPath    trait;
        })*/
     ,
     (TypeEquality,
      struct {
          HIRTypeRef type;
          HIRTypeRef otherType;
      })),
    (),
    (),
    (HIRGenericBound clone() const; Ordering ord(const HIRGenericBound& x) const;)
);
extern ::std::ostream& operator<<(::std::ostream& os, const HIRGenericBound& x);

class HIRGenericParams {
public:
    ::std::vector<HIRTypeParamDef> types;
    ::std::vector<HIRLifetimeDef> mLifetimes;
    ::std::vector<HIRValueParamDef> values;

    ::std::vector<HIRGenericBound> bounds;

    //GenericParams() {}

    HIRGenericParams clone() const;

    bool isEmpty() const;

    bool isGeneric() const;

    /// Create a PathParams instance that doesn't monomorphise at all
    HIRPathParams makeNopParams(HIRTypeInterner& types, unsigned level, bool lifetimesOnly = false) const;

    HIRPathParams makeEmptyParams(bool lifetimesOnly = false) const;

    struct PrintArgs {
        const HIRGenericParams& gp;

        PrintArgs(const HIRGenericParams& gp);

        friend ::std::ostream& operator<<(::std::ostream& os, const PrintArgs& x);
    };

    PrintArgs fmtArgs() const {
        return PrintArgs(*this);
    }

    struct PrintBounds {
        const HIRGenericParams& gp;

        PrintBounds(const HIRGenericParams& gp);

        friend ::std::ostream& operator<<(::std::ostream& os, const PrintBounds& x);
    };

    PrintBounds fmtBounds() const {
        return PrintBounds(*this);
    }

    Ordering ord(const HIRGenericParams& x) const;
};
