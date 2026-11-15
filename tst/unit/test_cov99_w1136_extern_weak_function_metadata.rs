//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(linkage)]

#[linkage = "extern_weak"]
pub fn cov99_optional_linkage() {}
