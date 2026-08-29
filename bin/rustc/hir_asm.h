#pragma once

#include "output.h"

#include <climits>
#include <string>
#include <vector>

#define ABI_RUST "Rust"
#define CRATE_BUILTINS "#builtins"

enum class AsmDirection {
    In,
    Out,
    LateOut,
    InOut,
    InLateOut
};

enum class AsmRegisterClass {
    x86Reg,
    x86RegAbcd,
    x86RegByte,
    x86Xmm,
    x86Ymm,
    x86Zmm,

    x86Kreg,

    riscvReg,
    riscvFreg,
};

#include "hir_asm_tu.h"

bool operator==(const AsmRegisterSpec& a, const AsmRegisterSpec& b);

static inline bool operator!=(const AsmRegisterSpec& a, const AsmRegisterSpec& b) {
    return !(a == b);
}

const char* to_string(const AsmRegisterClass& c);

struct AsmLineFragment {
    std::string before;

    unsigned index;
    char modifier;

    AsmLineFragment();

    bool operator==(const AsmLineFragment& x) const {
        return before == x.before && index == x.index && modifier == x.modifier;
    }
};

struct AsmLine {
    std::vector<AsmLineFragment> frags;
    std::string trailing;

    void fmt(stl::ZeroCopyOutput& os) const;

    bool operator==(const AsmLine& x) const {
        return frags == x.frags && trailing == x.trailing;
    }
};

struct AsmOptions {
    unsigned pure : 1;
    unsigned nomem : 1;
    unsigned readonly : 1;
    unsigned preservesFlags : 1;
    unsigned noreturn : 1;
    unsigned nostack : 1;
    unsigned attSyntax : 1;

    unsigned raw : 1;

    unsigned naked : 1;

    AsmOptions();

    bool any() const;

    void fmt(stl::ZeroCopyOutput& os) const;

    bool operator==(const AsmOptions& x) const;
};
