//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : "=r"($dst:expr) : : : $($flag:literal),+) => {{
        core::arch::asm!("xor {value:e}, {value:e}", value = lateout(reg) $dst, options(nomem, nostack));
    }};
    ($text:literal : "=&r"($dst:expr) : : : $($flag:literal),+) => {{
        core::arch::asm!("xor {value:e}, {value:e}", value = out(reg) $dst, options(nomem, nostack));
    }};
}
#[inline(never)]
fn choose(flag: u8) -> u32 {
    let result: u32;
    match flag {
        0 => unsafe { llvm_asm!("xor $0, $0" : "=r"(result) : : : "volatile", "intel"); },
        _ => unsafe { llvm_asm!("xor $0, $0" : "=&r"(result) : : : "volatile", "intel"); },
    }
    result
}
fn main() {
    assert_eq!(choose(std::hint::black_box(0)), 0);
    assert_eq!(choose(std::hint::black_box(1)), 0);
}
