#pragma once

#include "rc_string.h"

#include <cstdint>

/// Binding index for a Generic that indicates "Self"
#define GENERICSelf 0xFFFF
/// `Self` in the context of an erased type
#define GENERICErasedSelf 0xFFFE

enum HIRGenericGroup {
    GENERICImpl,
    GENERICItem,
    GENERICPlaceholder,
    GENERICHrtb,
};

struct HIRGenericRef {
    RcString name;
    // 0xFFFF = Self, 0-255 = Type/Trait, 256-511 = Method, 512-767 = Placeholder
    u32 binding;
    // Ordinary HIR generics and legacy named placeholders have scope zero.
    // Solver candidate existentials use an invocation-unique non-zero scope,
    // keeping their binder identity as typed data instead of an RcString.
    // They are transient and must be instantiated before HIR serialisation.
    u32 solverScope = 0;

    HIRGenericRef(RcString name, u32 binding);

    HIRGenericRef(RcString name, HIRGenericGroup group, u16 idx);

    static HIRGenericRef newSolverExistential(u32 scope, u16 idx);

    static HIRGenericRef newSelf() {
        return HIRGenericRef(RcString::newInterned("Self"), GENERICSelf);
    }

    bool isSelf() const {
        return binding == 0xFFFF;
    }

    unsigned idx() const {
        return binding & 0xFF;
    }

    unsigned group() const {
        return binding >> 8;
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

    void fmt(::std::ostream& os) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRGenericRef& x);
};
