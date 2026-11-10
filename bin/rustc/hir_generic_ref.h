#pragma once

#include "output.h"

#include "rc_string.h"

#include <cstdint>

#define GENERICSelf 0xFFFF

#define GENERICErasedSelf 0xFFFE

enum HIRGenericGroup {
    GENERICImpl,
    GENERICItem,
    GENERICPlaceholder,
    GENERICHrtb,
};

struct HIRGenericRef {
    RcString name;

    u32 binding;

    u32 solverScope = 0;

    HIRGenericRef(RcString name, u32 binding);

    HIRGenericRef(RcString name, HIRGenericGroup group, u32 idx);

    static constexpr u32 MAX_INDEX = 0x7FFFFF;

    static u32 makeBinding(unsigned group, u32 index);

    static unsigned indexOf(u32 binding) {
        return (binding & 0xFF) | ((binding >> 16) << 8);
    }

    static unsigned groupOf(u32 binding) {
        return (binding >> 8) & 0xFF;
    }

    static HIRGenericRef newSolverExistential(u32 scope, u32 idx);

    bool isSelf() const {
        return binding == 0xFFFF;
    }

    unsigned idx() const {
        return indexOf(binding);
    }

    unsigned group() const {
        return groupOf(binding);
    }

    bool isSolverExistential() const {
        return solverScope != 0;
    }

    bool isPlaceholder() const {
        return group() == GENERICPlaceholder;
    }

    Ordering ord(const HIRGenericRef& x) const;

    bool operator==(const HIRGenericRef& x) const {
        return this->ord(x) == OrdEqual;
    }

    bool operator!=(const HIRGenericRef& x) const {
        return this->ord(x) != OrdEqual;
    }

    bool operator<(const HIRGenericRef& x) const {
        return this->ord(x) == OrdLess;
    }

    void fmt(stl::ZeroCopyOutput& os) const;

};
