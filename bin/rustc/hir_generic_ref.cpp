#include "hir_generic_ref.h"

#include "output.h"

using namespace stl;

HIRGenericRef::HIRGenericRef(RcString name, u32 binding)
    : name(std::move(name))
    , binding(binding)
{
}

HIRGenericRef::HIRGenericRef(RcString name, HIRGenericGroup group, u16 idx)
    : name(std::move(name))
    , binding(group * 256 + idx)
{
    BUG_ASSERT(idx < 256);
}

HIRGenericRef HIRGenericRef::newSolverExistential(u32 scope, u16 idx) {
    BUG_ASSERT(scope != 0);
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
        return ::ord(name, x.name);
    }
    return rv;
}

void HIRGenericRef::fmt(ZeroCopyOutput& os) const {
    os << this->name << StringView("/*");
    if (this->isSolverExistential()) {
        os << StringView("E:") << this->solverScope << StringView(":") << this->idx();
    } else if (this->binding == GENERICSelf) {
        os << StringView("");
    } else {
        switch (this->group()) {
            case 0:
                os << StringView("I:") << this->idx();
                break;
            case 1:
                os << StringView("M:") << this->idx();
                break;
            case 2:
                os << StringView("P:") << this->idx();
                break;
            case 3:
                os << StringView("H:") << this->idx();
                break;
            default:
                os << this->binding;
                break;
        }
    }
    os << StringView("*/");
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, HIRGenericRef>(ZeroCopyOutput& os, HIRGenericRef x) {
        x.fmt(os);
        return;
    }
}
