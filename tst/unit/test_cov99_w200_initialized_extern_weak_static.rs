//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Cemit-cpp-only
//@ output-contains: .cpp __attribute__((weak_import))
#![no_std]
#![feature(linkage)]

#[no_mangle]
#[linkage = "extern_weak"]
pub static COV99_W200_WEAK_INITIALIZER: u8 = 29;
