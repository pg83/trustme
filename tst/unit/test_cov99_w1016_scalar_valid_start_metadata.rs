//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(rustc_attrs)]

#[repr(transparent)]
#[rustc_layout_scalar_valid_range_start(1)]
pub struct NonZeroByte(u8);
