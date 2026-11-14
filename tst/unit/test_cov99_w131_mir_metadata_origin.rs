//@ crate-type: lib
//@ compile-flags: --emit=metadata

#![no_std]
#![crate_name = "cov99_w131_mir_metadata_origin"]

#[inline]
pub fn incomplete_payload<T>() {}
