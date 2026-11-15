//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Cemit-cpp-only -Cpanic=abort -Zmir-opt-level=0 -Zdump-mir

#![no_std]

#[unsafe(naked)]
#[no_mangle]
pub unsafe extern "C" fn cov99_w1418_naked_unit() {
    core::arch::naked_asm!("ret");
}
