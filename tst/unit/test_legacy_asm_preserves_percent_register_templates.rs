//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $constraint:literal($dst:expr) : : : $flag:literal) => {{
        core::arch::asm!($text, out("eax") $dst, options(att_syntax, nostack));
    }};
}
fn main() {
    let result: u32;
    unsafe {
        llvm_asm!("xorl %eax, %eax\nincl %eax\n# single %; paired %% ; operand %0\n"
            : "={eax}"(result) : : : "volatile");
    }
    assert_eq!(result, 1);
}
