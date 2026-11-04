//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $output:literal($dst:expr) : : "cc" : $($flag:literal),+) => {{
        core::arch::asm!($text, out("eax") $dst, options(nomem, nostack));
    }};
    ($text:literal : $output:literal($dst:expr) : : "memory" : $($flag:literal),+) => {{
        core::arch::asm!($text, out("eax") $dst, options(nostack));
    }};
}
#[inline(never)]
fn choose(flag: u8) -> u32 {
    let result: u32;
    match flag {
        0 => unsafe { llvm_asm!("mov eax, 541" : "={eax}"(result) : : "cc" : "volatile", "intel"); },
        _ => unsafe { llvm_asm!("mov eax, 541" : "={eax}"(result) : : "memory" : "volatile", "intel"); },
    }
    result
}
fn main() {
    assert_eq!(choose(std::hint::black_box(0)), 541);
    assert_eq!(choose(std::hint::black_box(1)), 541);
}
