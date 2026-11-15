//@ crate-type: lib
//@ compile-flags: --emit=metadata

#![no_std]
#![feature(rustc_attrs)]

#[repr(transparent)]
#[rustc_layout_scalar_valid_range_end(7)]
pub struct EndBounded(u8);
