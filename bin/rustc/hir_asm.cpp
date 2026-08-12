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
    , attSyntax(0) {
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
    _(attSyntax);
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
    _(attSyntax);
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
    _(attSyntax);
    _(naked);
#undef _
    return true;
}
}

namespace AsmCommon {

std::ostream& operator<<(std::ostream& os, const Direction& d) {
    switch (d) {
        case Direction::In:
            return os << "in";
        case Direction::Out:
            return os << "out";
        case Direction::LateOut:
            return os << "lateout";
        case Direction::InOut:
            return os << "inout";
        case Direction::InLateOut:
            return os << "inlateout";
    }
    return os;
}
bool operator==(const RegisterSpec& a, const RegisterSpec& b) {
    if (a.tag() != b.tag()) {
        return false;
    }
    TU_MATCH_HDRA( (a,b), {)
    TU_ARMA(Class, ae,be)
        return ae == be;
        TU_ARMA(Explicit, ae, be)
        return ae == be;
    }
    return true;
}
const char* to_string(const RegisterClass& c) {
    switch (c) {
        case RegisterClass::x86_reg:
            return "reg";
        case RegisterClass::x86_reg_abcd:
            return "reg_abcd";
        case RegisterClass::x86_reg_byte:
            return "reg_byte";
        case RegisterClass::x86_xmm:
            return "xmm_reg";
        case RegisterClass::x86_ymm:
            return "ymm_reg";
        case RegisterClass::x86_zmm:
            return "zmm_reg";
        case RegisterClass::x86_kreg:
            return "kreg";
        case RegisterClass::riscv_reg:
            return "reg";
        case RegisterClass::riscv_freg:
            return "freg";
    }
    throw "";
}
std::ostream& operator<<(std::ostream& os, const RegisterSpec& s) {
    TU_MATCH_HDRA((s), {)
    TU_ARMA(Class, c) {
            os << to_string(c);
        }
        TU_ARMA(Explicit, e) {
            os << "\"" << e << "\"";
        }
    }
    return os;
}
}
