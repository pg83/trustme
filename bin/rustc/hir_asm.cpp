#include "hir_asm.h"

#include "span.h"
#include "output.h"

#include <cctype>

using namespace stl;

namespace {
    void printFmtString(ZeroCopyOutput& out, const std::string& value) {
        static const char* hex = "0123456789ABCDEF";
        for (auto c : value) {
            if (c == '{') {
                out << StringView("{{");
            } else if (c == '\\') {
                out << StringView("\\\\");
            } else if (c == '"') {
                out << StringView("\\\"");
            } else if (std::isprint(c)) {
                out << c;
            } else {
                out << StringView("\\x") << hex[c >> 4] << hex[c & 15];
            }
        }
    }
}

AsmLineFragment::AsmLineFragment()
    : index(UINT_MAX)
    , modifier('\0')
{
}

void AsmLine::fmt(ZeroCopyOutput& out) const {
    out << StringView("\"");
    for (const auto& fragment : frags) {
        printFmtString(out, fragment.before);
        out << StringView("{") << fragment.index;
        if (fragment.modifier) {
            out << StringView(":") << fragment.modifier;
        }
        out << StringView("}");
    }
    printFmtString(out, trailing);
    out << StringView("\"");
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

void AsmOptions::fmt(ZeroCopyOutput& os) const {
    os << StringView("options(");
#define _(n) \
    if (n)   \
    os << StringView(#n ",")
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
    os << StringView(")");
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
    UNREACHABLE();
}

template <>
void stl::output<ZeroCopyOutput, AsmRegisterClass>(ZeroCopyOutput& out, AsmRegisterClass value) {
    out << StringView(to_string(value));
}

template <>
void stl::output<ZeroCopyOutput, AsmOptions>(ZeroCopyOutput& out, AsmOptions value) {
    value.fmt(out);
}

template <>
void stl::output<ZeroCopyOutput, std::vector<AsmLine>>(ZeroCopyOutput& out, const std::vector<AsmLine>& values) {
    outCont(out, values);
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, AsmLine>(ZeroCopyOutput& os, const AsmLine& line) {
        line.fmt(os);
    }

    template <>
    void output<ZeroCopyOutput, AsmDirection>(ZeroCopyOutput& os, AsmDirection d) {
        switch (d) {
            case AsmDirection::In:
                os << StringView("in");
                return;
            case AsmDirection::Out:
                os << StringView("out");
                return;
            case AsmDirection::LateOut:
                os << StringView("lateout");
                return;
            case AsmDirection::InOut:
                os << StringView("inout");
                return;
            case AsmDirection::InLateOut:
                os << StringView("inlateout");
                return;
        }
        return;
    }
}
