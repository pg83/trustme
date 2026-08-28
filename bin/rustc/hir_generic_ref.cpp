#include "hir_generic_ref.h"

HIRGenericRef::HIRGenericRef(RcString name, u32 binding)
    : name(::std::move(name))
    , binding(binding)
{
}

HIRGenericRef::HIRGenericRef(RcString name, HIRGenericGroup group, u16 idx)
    : name(::std::move(name))
    , binding(group * 256 + idx)
{
    assert(idx < 256);
}

HIRGenericRef HIRGenericRef::newSolverExistential(u32 scope, u16 idx) {
    assert(scope != 0);
    auto result = HIRGenericRef(RcString(), GENERICPlaceholder, idx);
    result.solverScope = scope;
    return result;
}

Ordering HIRGenericRef::ord(const HIRGenericRef& x) const {
    auto rv = ::ord(binding, x.binding);
    if (rv) {
        return rv;
    }
    if (group() == GENERICPlaceholder) {
        rv = ::ord(solverScope, x.solverScope);
        if (rv || isSolverExistential()) {
            return rv;
        }
        return ::ord(name, x.name); // names matter for legacy placeholders
    }
    return rv;
}

::std::ostream& operator<<(::std::ostream& os, const HIRGenericRef& x) {
    x.fmt(os);
    return os;
}
