#pragma once

/*
 */
#include "tagged_union.h"
#include <climits>
#include <ostream>
#include <string>
#include <vector>

// ABI and builtin crate names are shared by the AST and HIR assembly representations.
#define ABI_RUST "Rust"
#define CRATE_BUILTINS "#builtins" // used for macro re-exports of builtins

enum class AsmDirection {
    In,
    Out,
    LateOut,
    InOut,
    InLateOut
};

std::ostream& operator<<(std::ostream& os, const AsmDirection& d);

enum class AsmRegisterClass {
    x86Reg,
    x86RegAbcd,
    x86RegByte,
    x86Xmm,
    x86Ymm,
    x86Zmm,
    //x86_mm, // Requires
    x86Kreg,

    //aarch64_reg,
    //aarch64_vreg,

    //arm_reg,
    //arm_sreg,
    //arm_dreg,
    //arm_qreg,

    //mips_reg,
    //mips_freg,

    //nvptx_reg16,
    //nvptx_reg32,
    //nvptx_reg64,

    riscvReg,
    riscvFreg,

    //hexagon_reg,

    //powerpc_reg,
    //powerpc_reg_nonzero,
    //powerpc_freg,

    //wasm32_local,

    //bpf_reg,
    //bpf_wreg,
};

// Definitions generated from hir_asm.tu.
#include "hir_asm_tu.h"

bool operator==(const AsmRegisterSpec& a, const AsmRegisterSpec& b);

static inline bool operator!=(const AsmRegisterSpec& a, const AsmRegisterSpec& b) {
    return !(a == b);
}

const char* to_string(const AsmRegisterClass& c);

std::ostream& operator<<(std::ostream& os, const AsmRegisterSpec& s);

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

    void fmt(std::ostream& os) const;

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
    /// The template is the assembly as written: no `{}` substitution.
    unsigned raw : 1;
    // Indicates `naked_asm!`
    unsigned naked : 1;

    AsmOptions();

    bool any() const;

    void fmt(std::ostream& os) const;

    bool operator==(const AsmOptions& x) const;
};
