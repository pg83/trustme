//@ compile-fail: scalar_valid_range_end] requires a struct
#![feature(rustc_attrs)]

#[rustc_layout_scalar_valid_range_end(7)]
fn invalid() -> u8 { 3 }

fn main() {}
