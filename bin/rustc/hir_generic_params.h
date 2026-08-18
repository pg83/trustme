#pragma once

#include "hir_path.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"

#include <string>
#include <vector>
#include <iostream>

struct HIRTypeParamDef {
    RcString name;
    HIRTypeRef defaultValue;
    bool isSized;

    Ordering ord(const HIRTypeParamDef& x) const;
};

struct HIRValueParamDef {
    RcString name;
    HIRTypeRef type;
    HIRConstGeneric defaultValue;

    Ordering ord(const HIRValueParamDef& x) const;
};

class HIRGenericParams;

// Definitions generated from hir_generic_params.tu.
#include "hir_generic_params_tu.h"
extern ::std::ostream& operator<<(::std::ostream& os, const HIRGenericBound& x);

class HIRGenericParams {
public:
    ::std::vector<HIRTypeParamDef> types;
    ::std::vector<HIRValueParamDef> values;

    ::std::vector<HIRGenericBound> bounds;

    //GenericParams() {}

    HIRGenericParams clone() const;

    bool isEmpty() const;

    bool isGeneric() const;

    /// Create a PathParams instance that doesn't monomorphise at all
    HIRPathParams makeNopParams(HIRTypeInterner& types, unsigned level) const;

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
