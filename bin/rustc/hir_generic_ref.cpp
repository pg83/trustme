#include "hir_generic_ref.h"

#include "output.h"

using namespace stl;

HIRGenericRef::HIRGenericRef(RcString name, u32 binding)
    : name(std::move(name))
    , binding(binding)
{
}

HIRGenericRef::HIRGenericRef(RcString name, HIRGenericGroup group, u32 idx)
    : name(std::move(name))
    , binding(makeBinding(group, idx))
{
}

u32 HIRGenericRef::makeBinding(unsigned group, u32 index) {
    BUG_ASSERT(group < 256);
    BUG_ASSERT(index <= MAX_INDEX);
    return (group << 8) | (index & 0xFF) | ((index >> 8) << 16);
}

HIRGenericRef HIRGenericRef::newSolverExistential(u32 scope, u32 idx) {
    BUG_ASSERT(scope != 0);
    auto result = HIRGenericRef(RcString(), GENERICPlaceholder, idx);
    result.solverScope = scope;
    return result;
}

Ordering HIRGenericRef::ord(const HIRGenericRef& x) const {
    auto rv = ::ord(group(), x.group());
    if (!rv) {
        rv = ::ord(idx(), x.idx());
    }
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

template <>
void stl::output<ZeroCopyOutput, HIRGenericRef>(ZeroCopyOutput& os, HIRGenericRef x) {
    x.fmt(os);
    return;
}
