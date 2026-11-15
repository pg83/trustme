//@ crate-type: lib
//@ compile-only
//@ compile-fail: Naked function body does not contain naked_asm
//@ compile-flags: -Cemit-cpp-only -Cpanic=abort -Zmir-opt-level=0 -Zdump-mir

#![no_std]

#[unsafe(naked)]
#[no_mangle]
pub unsafe extern "C" fn cov99_w1418_naked_empty_body() {}
