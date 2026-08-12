#pragma once

#include <cstdint>
#include "rc_string.h"

/// Binding index for a Generic that indicates "Self"
#define GENERICSelf 0xFFFF
/// `Self` in the context of an erased type
#define GENERICErasedSelf 0xFFFE

namespace HIR {

    enum GenericGroup {
        GENERICImpl,
        GENERICItem,
        GENERICPlaceholder,
        GENERICHrtb,
    };

    struct GenericRef {
        RcString name;
        // 0xFFFF = Self, 0-255 = Type/Trait, 256-511 = Method, 512-767 = Placeholder
        uint32_t binding;

        GenericRef(RcString name, uint32_t binding);

        GenericRef(RcString name, GenericGroup group, uint16_t idx);

        static GenericRef newSelf() {
            return GenericRef(RcString::newInterned("Self"), GENERICSelf);
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

        bool is_placeholder() const {
            return (binding >> 8) == GENERICPlaceholder;
        }

        Ordering ord(const GenericRef& x) const;

        bool operator==(const GenericRef& x) const {
            return this->ord(x) == OrdEqual;
        }

        bool operator!=(const GenericRef& x) const {
            return this->ord(x) != OrdEqual;
        }

        bool operator<(const GenericRef& x) const {
            return this->ord(x) == OrdLess;
        }

        void fmt(::std::ostream& os) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const GenericRef& x);
    };

    struct LifetimeRef {
        static const uint32_t STATIC = 0xFFFF;  // `'static`
        static const uint32_t UNKNOWN = 0xFFFE; // omitted
        static const uint32_t INFER = 0xFFFD;   // `'_`
        static const uint32_t MAX_LOCAL = 0x8'0000;

        //RcString  name;
        // Values below 2^16 are parameters/static, values above are per-function region IDs allocated during region inferrence.
        uint32_t binding = UNKNOWN;

        LifetimeRef();

        LifetimeRef(uint32_t binding);

        static LifetimeRef new_static();

        bool isParam() const {
            return binding < 0xFF00;
        }

        GenericRef asParam() const;

        bool isHrl() const {
            return isParam() && asParam().group() == 3;
        }

        Ordering ord(const LifetimeRef& x) const {
            return ::ord(binding, x.binding);
        }

        bool operator<(const LifetimeRef& x) const {
            return this->ord(x) == OrdLess;
        }

        bool operator==(const LifetimeRef& x) const {
            return binding == x.binding;
        }

        bool operator!=(const LifetimeRef& x) const {
            return !(*this == x);
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const LifetimeRef& x);
    };

}
