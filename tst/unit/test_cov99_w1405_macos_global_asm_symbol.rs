//@ crate-type: lib
//@ compile-flags: --target=x86_64-apple-darwin -Cemit-cpp-only
//@ output-contains: .cpp .quad _trustme_cov99_w1405_entry
#![no_std]

#[no_mangle]
pub extern "C" fn trustme_cov99_w1405_entry() {}

core::arch::global_asm!(".quad {entry}", entry = sym trustme_cov99_w1405_entry);
