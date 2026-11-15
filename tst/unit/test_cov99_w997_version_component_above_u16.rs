//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ env: RUSTC_OVERRIDE_VERSION_STRING=1.65536.0

#![no_std]

pub const COMPONENT_BOUNDARY: u32 = 65536;
