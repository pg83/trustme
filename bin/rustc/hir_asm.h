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

namespace AsmCommon {

    enum class Direction {
        In,
        Out,
        LateOut,
        InOut,
        InLateOut
    };

    std::ostream& operator<<(std::ostream& os, const Direction& d);

    enum class RegisterClass {
        x86_reg,
        x86_reg_abcd,
        x86_reg_byte,
        x86_xmm,
        x86_ymm,
        x86_zmm,
        //x86_mm, // Requires
        x86_kreg,

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

    TAGGED_UNION_EX(RegisterSpec, (), Explicit, ((Class, RegisterClass), (Explicit, std::string)), (), (), (RegisterSpec clone() const {
                TU_MATCH_HDRA((*this),{)
                TU_ARMA(Class, e)   return e;
            TU_ARMA(Explicit, e) return e;
                }
                throw "";
                    }));

    bool operator==(const RegisterSpec& a, const RegisterSpec& b);

    static inline bool operator!=(const RegisterSpec& a, const RegisterSpec& b) {
        return !(a == b);
    }

    const char* to_string(const RegisterClass& c);

    std::ostream& operator<<(std::ostream& os, const RegisterSpec& s);

    struct LineFragment {
        std::string before;

        unsigned index;
        char modifier;

        LineFragment();

        bool operator==(const LineFragment& x) const {
            return before == x.before && index == x.index && modifier == x.modifier;
        }
    };

    struct Line {
        std::vector<LineFragment> frags;
        std::string trailing;

        void fmt(std::ostream& os) const;

        bool operator==(const Line& x) const {
            return frags == x.frags && trailing == x.trailing;
        }
    };

    struct Options {
        unsigned pure : 1;
        unsigned nomem : 1;
        unsigned readonly : 1;
        unsigned preservesFlags : 1;
        unsigned noreturn : 1;
        unsigned nostack : 1;
        unsigned attSyntax : 1;
        // Indicates `naked_asm!`
        unsigned naked : 1;

        Options();

        bool any() const;

        void fmt(std::ostream& os) const;

        bool operator==(const Options& x) const;
    };
}
