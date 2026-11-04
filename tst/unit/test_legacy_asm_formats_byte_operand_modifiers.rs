//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $output:literal($dst:expr) : $input:literal($src:expr) : $clobber:literal : $flag:literal) => {{
        core::arch::asm!("test {value:e}, {value:e}", "setnz {result}",
            value = in(reg) $src, result = lateout(reg_byte) $dst,
            options(nomem, nostack));
    }};
}
#[inline(never)]
fn nonzero(input: u32) -> u8 {
    let result: u8;
    unsafe {
        llvm_asm!("testl $1, $1\nsetnz ${0:b}"
            : "=q"(result) : "r"(input) : "cc" : "volatile");
    }
    result
}
fn main() {
    assert_eq!(nonzero(std::hint::black_box(0)), 0);
    assert_eq!(nonzero(std::hint::black_box(5)), 1);
}
