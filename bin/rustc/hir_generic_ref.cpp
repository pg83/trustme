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

