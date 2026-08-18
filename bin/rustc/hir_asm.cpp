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
    , raw(0)
    , naked(0)
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
    _(raw);
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
    _(raw);
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
    _(raw);
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
    switch (a.tag()) {
        case AsmRegisterSpec::TAG_Class: {
            auto& ae = a.as_Class();
            auto& be = b.as_Class();
            return ae == be;
        }
        case AsmRegisterSpec::TAG_Explicit: {
            auto& ae = a.as_Explicit();
            auto& be = b.as_Explicit();
            return ae == be;
        }
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
    switch (s.tag()) {
        case AsmRegisterSpec::TAG_Class: {
            auto& c = s.as_Class();
            os << to_string(c);
            break;
        }
        case AsmRegisterSpec::TAG_Explicit: {
            auto& e = s.as_Explicit();
            os << "\"" << e << "\"";
            break;
        }
    }
    return os;
}
