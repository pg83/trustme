#include "hir_asm.h"

AsmLineFragment::AsmLineFragment()
    : index(UINT_MAX)
    , modifier('\0')
{
}

AsmOptions::AsmOptions()
    : pure(0)
    , nomem(0)
    , readonly(0)
    , preservesFlags(0)
    , noreturn(0)
    , nostack(0)
    , attSyntax(0)
{
}

bool AsmOptions::any() const {
#define _(n) \
    if (n)   \
    return true
    _(pure);
    _(nomem);
    _(readonly);
    _(preservesFlags);
    _(noreturn);
    _(nostack);
    _(attSyntax);
    _(naked);
#undef _
    return false;
}

void AsmOptions::fmt(std::ostream& os) const {
    os << "options(";
#define _(n) \
    if (n)   \
    os << #n ","
    _(pure);
    _(nomem);
    _(readonly);
    _(preservesFlags);
    _(noreturn);
    _(nostack);
    _(attSyntax);
    _(naked);
#undef _
    os << ")";
}

bool AsmOptions::operator==(const AsmOptions& x) const {
#define _(n)      \
    if (n != x.n) \
    return false
    _(pure);
    _(nomem);
    _(readonly);
    _(preservesFlags);
    _(noreturn);
    _(nostack);
    _(attSyntax);
    _(naked);
#undef _
    return true;
}

std::ostream& operator<<(std::ostream& os, const AsmDirection& d) {
    switch (d) {
        case AsmDirection::In:
            return os << "in";
        case AsmDirection::Out:
            return os << "out";
        case AsmDirection::LateOut:
            return os << "lateout";
        case AsmDirection::InOut:
            return os << "inout";
        case AsmDirection::InLateOut:
            return os << "inlateout";
    }
    return os;
}

bool operator==(const AsmRegisterSpec& a, const AsmRegisterSpec& b) {
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

const char* to_string(const AsmRegisterClass& c) {
    switch (c) {
        case AsmRegisterClass::x86Reg:
            return "reg";
        case AsmRegisterClass::x86RegAbcd:
            return "reg_abcd";
        case AsmRegisterClass::x86RegByte:
            return "reg_byte";
        case AsmRegisterClass::x86Xmm:
            return "xmm_reg";
        case AsmRegisterClass::x86Ymm:
            return "ymm_reg";
        case AsmRegisterClass::x86Zmm:
            return "zmm_reg";
        case AsmRegisterClass::x86Kreg:
            return "kreg";
        case AsmRegisterClass::riscvReg:
            return "reg";
        case AsmRegisterClass::riscvFreg:
            return "freg";
    }
    throw "";
}

std::ostream& operator<<(std::ostream& os, const AsmRegisterSpec& s) {
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
