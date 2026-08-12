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
    if (group() == GENERIC_Placeholder) {
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
