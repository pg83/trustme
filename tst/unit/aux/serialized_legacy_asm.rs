#![feature(rustc_attrs)]
#![allow(internal_features)]
#[cfg_attr(rust_compiler = "trustme", rustc_builtin_macro)]
macro_rules! llvm_asm {
    ($($tokens:tt)*) => { core::arch::asm!("", options(nomem, nostack)); };
}
#[inline]
pub unsafe fn preserve<T: Copy>(value: T) -> T {
    llvm_asm!("" :::: "volatile");
    value
}
