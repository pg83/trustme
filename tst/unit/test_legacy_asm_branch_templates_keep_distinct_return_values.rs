//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $output:literal($dst:expr) : : : $($flag:literal),+) => {{
        core::arch::asm!($text, out("eax") $dst, options(nomem, nostack));
    }};
}
#[inline(never)]
fn choose(flag: u8) -> u32 {
    let result: u32;
    match flag {
        0 => unsafe { llvm_asm!("mov eax, 503" : "={eax}"(result) : : : "volatile", "intel"); },
        _ => unsafe { llvm_asm!("mov eax, 509" : "={eax}"(result) : : : "volatile", "intel"); },
    }
    result
}
fn main() {
    assert_eq!(choose(std::hint::black_box(0)), 503);
    assert_eq!(choose(std::hint::black_box(1)), 509);
}
