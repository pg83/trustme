//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only

#![no_std]
#![crate_name = "cov99_w131_mir_metadata_seed"]

#[inline]
pub fn incomplete_payload<T>() {}
