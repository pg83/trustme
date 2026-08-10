// Extracted from src/inline-assembly.md:707
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x = 5i32;
    let y = -1i8;
    let z = unsafe { core::arch::x86_64::_mm_set_epi64x(1, 0) };
    
    // reg is valid for `i32`, `reg_byte` is valid for `i8`, and xmm_reg is valid for `__m128i`
    // We can't use `tmm0` as an input or output, but we can clobber it.
    unsafe { core::arch::asm!("/* {} {} {} */", in(reg) x, in(reg_byte) y, in(xmm_reg) z, out("tmm0") _); }
    }
}
