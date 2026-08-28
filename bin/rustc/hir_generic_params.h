#pragma once

#include "hir_path.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"

#include <std/lib/vector.h>

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

enum class HIRGenericParamKind : u8 {
    Type,
    Value,
};

class HIRGenericParams;

#include "hir_generic_params_tu.h"
std::ostream& operator<<(std::ostream& os, const HIRGenericBound& x);

class HIRGenericParams {
public:
    std::vector<HIRTypeParamDef> types;
    std::vector<HIRValueParamDef> values;

    stl::Vector<HIRGenericParamKind> paramKinds;

    std::vector<HIRGenericBound> bounds;

    HIRGenericParams clone() const;

    bool isEmpty() const;

    bool isGeneric() const;

    size_t paramCount() const {
        return types.size() + values.size();
    }

    bool hasParamOrder() const {
        return paramKinds.length() == paramCount();
    }

    HIRGenericParamKind paramKindAt(size_t index) const {
        if (hasParamOrder()) {
            return paramKinds[index];
        }
        return index < types.size() ? HIRGenericParamKind::Type : HIRGenericParamKind::Value;
    }

    HIRPathParams makeNopParams(HIRTypeInterner& types, unsigned level) const;

    struct PrintArgs {
        const HIRGenericParams& gp;

        PrintArgs(const HIRGenericParams& gp);

        friend std::ostream& operator<<(std::ostream& os, const PrintArgs& x);
    };

    PrintArgs fmtArgs() const {
        return PrintArgs(*this);
    }

    struct PrintBounds {
        const HIRGenericParams& gp;

        PrintBounds(const HIRGenericParams& gp);

        friend std::ostream& operator<<(std::ostream& os, const PrintBounds& x);
    };

    PrintBounds fmtBounds() const {
        return PrintBounds(*this);
    }

    Ordering ord(const HIRGenericParams& x) const;
};
