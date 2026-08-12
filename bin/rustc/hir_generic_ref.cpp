#include "hir_generic_ref.h"

namespace HIR {

GenericRef::GenericRef(RcString name, uint32_t binding)
    : name(::std::move(name))
    , binding(binding) {
}
GenericRef::GenericRef(RcString name, GenericGroup group, uint16_t idx)
    : name(::std::move(name))
    , binding(group * 256 + idx) {
    assert(idx < 256);
}
Ordering GenericRef::ord(const GenericRef& x) const {
    auto rv = ::ord(binding, x.binding);
    if (rv) {
        return rv;
    }
    if (group() == GENERICPlaceholder) {
        return ::ord(name, x.name); // names matter for placeholders
    }
    return rv;
}
}

namespace HIR {

::std::ostream& operator<<(::std::ostream& os, const GenericRef& x) {
    x.fmt(os);
    return os;
}
}

namespace HIR {

LifetimeRef::LifetimeRef()
    : binding(LifetimeRef::UNKNOWN) {
}
LifetimeRef::LifetimeRef(uint32_t binding)
    : binding(binding) {
}
LifetimeRef LifetimeRef::new_static() {
    LifetimeRef rv;
    rv.binding = LifetimeRef::STATIC;
    return rv;
}
GenericRef LifetimeRef::asParam() const {
    assert(is_param());
    return GenericRef(RcString(), binding);
}
::std::ostream& operator<<(::std::ostream& os, const LifetimeRef& x) {
    if (x.binding == LifetimeRef::INFER) {
        os << "'_";
    } else if (x.binding == LifetimeRef::UNKNOWN) {
        os << "'#omitted";
    } else if (x.binding == LifetimeRef::STATIC) {
        os << "'static";
    } else if (x.binding < 0xFFFF) {
        switch ((x.binding & 0xFF00) >> 8) {
            case 0:
                os << "'I" << (x.binding & 0xFF);
                break; // Impl/type
            case 1:
                os << "'M" << (x.binding & 0xFF);
                break; // Method/value
            case 2:
                os << "'P" << (x.binding & 0xFF);
                break; // HRLS
            case 3:
                os << "'H" << (x.binding & 0xFF);
                break; // HRLS
            default:
                os << "'unk" << std::hex << x.binding << std::dec;
                break;
        }
    } else if (x.binding < LifetimeRef::MAX_LOCAL) {
        os << "'#local" << (x.binding - 0x1'0000);
    } else {
        os << "'#ivar" << (x.binding - LifetimeRef::MAX_LOCAL);
    }
    return os;
}
}
