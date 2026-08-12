#include "hir_asm.h"

namespace AsmCommon {

LineFragment::LineFragment()
    : index(UINT_MAX)
    , modifier('\0') {
}
Options::Options()
    : pure(0)
    , nomem(0)
    , readonly(0)
    , preserves_flags(0)
    , noreturn(0)
    , nostack(0)
    , att_syntax(0) {
}
bool Options::any() const {
#define _(n) \
    if (n)   \
    return true
    _(pure);
    _(nomem);
    _(readonly);
    _(preserves_flags);
    _(noreturn);
    _(nostack);
    _(att_syntax);
    _(naked);
#undef _
    return false;
}
void Options::fmt(std::ostream& os) const {
    os << "options(";
#define _(n) \
    if (n)   \
    os << #n ","
    _(pure);
    _(nomem);
    _(readonly);
    _(preserves_flags);
    _(noreturn);
    _(nostack);
    _(att_syntax);
    _(naked);
#undef _
    os << ")";
}
bool Options::operator==(const Options& x) const {
#define _(n)      \
    if (n != x.n) \
    return false
    _(pure);
    _(nomem);
    _(readonly);
    _(preserves_flags);
    _(noreturn);
    _(nostack);
    _(att_syntax);
    _(naked);
#undef _
    return true;
}
}
