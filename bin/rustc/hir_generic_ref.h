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
    uint32_t binding;

    HIRGenericRef(RcString name, uint32_t binding);

    HIRGenericRef(RcString name, HIRGenericGroup group, uint16_t idx);

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

    bool isPlaceholder() const {
        return (binding >> 8) == GENERICPlaceholder;
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
