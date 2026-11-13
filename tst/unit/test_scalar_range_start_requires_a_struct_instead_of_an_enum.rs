//@ compile-fail: scalar_valid_range_start] requires a struct
#![feature(rustc_attrs)]

#[rustc_layout_scalar_valid_range_start(1)]
enum Invalid {
    Value(u8),
}

fn main() {}
