#include "hir_generic_ref.h"

HIRGenericRef::HIRGenericRef(RcString name, uint32_t binding)
    : name(::std::move(name))
    , binding(binding)
{
}

HIRGenericRef::HIRGenericRef(RcString name, HIRGenericGroup group, uint16_t idx)
    : name(::std::move(name))
    , binding(group * 256 + idx)
{
    assert(idx < 256);
}

Ordering HIRGenericRef::ord(const HIRGenericRef& x) const {
    auto rv = ::ord(binding, x.binding);
    if (rv) {
        return rv;
    }
    if (group() == GENERICPlaceholder) {
        return ::ord(name, x.name); // names matter for placeholders
    }
    return rv;
}

::std::ostream& operator<<(::std::ostream& os, const HIRGenericRef& x) {
    x.fmt(os);
    return os;
}

HIRLifetimeRef::HIRLifetimeRef()
    : binding(HIRLifetimeRef::UNKNOWN)
{
}

HIRLifetimeRef::HIRLifetimeRef(uint32_t binding)
    : binding(binding)
{
}

HIRLifetimeRef HIRLifetimeRef::newStatic() {
    HIRLifetimeRef rv;
    rv.binding = HIRLifetimeRef::STATIC;
    return rv;
}

HIRGenericRef HIRLifetimeRef::asParam() const {
    assert(isParam());
    return HIRGenericRef(RcString(), binding);
}

::std::ostream& operator<<(::std::ostream& os, const HIRLifetimeRef& x) {
    if (x.binding == HIRLifetimeRef::INFER) {
        os << "'_";
    } else if (x.binding == HIRLifetimeRef::UNKNOWN) {
        os << "'#omitted";
    } else if (x.binding == HIRLifetimeRef::STATIC) {
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
    } else if (x.binding < HIRLifetimeRef::MAX_LOCAL) {
        os << "'#local" << (x.binding - 0x1'0000);
    } else {
        os << "'#ivar" << (x.binding - HIRLifetimeRef::MAX_LOCAL);
    }
    return os;
}
