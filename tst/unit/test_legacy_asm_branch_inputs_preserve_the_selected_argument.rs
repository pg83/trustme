//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($text:literal : $output:literal($dst:expr) : $input:literal($src:expr) : : $($flag:literal),+) => {{
        $dst = $src;
        core::arch::asm!($text, inout("eax") $dst, options(nomem, nostack));
    }};
}
#[inline(never)]
fn choose(flag: u8, first: u32, second: u32) -> u32 {
    let mut result: u32;
    match flag {
        0 => unsafe { llvm_asm!("add eax, 3" : "={eax}"(result) : "{eax}"(first) : : "volatile", "intel"); },
        _ => unsafe { llvm_asm!("add eax, 3" : "={eax}"(result) : "{eax}"(second) : : "volatile", "intel"); },
    }
    result
}
fn main() {
    assert_eq!(choose(std::hint::black_box(0), 521, 523), 524);
    assert_eq!(choose(std::hint::black_box(1), 521, 523), 526);
}
