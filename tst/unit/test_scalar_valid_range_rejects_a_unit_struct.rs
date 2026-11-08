//@ compile-fail: Invalid use of #[rustc_layout_scalar_valid_range_start]
#![feature(rustc_attrs)]
#[rustc_layout_scalar_valid_range_start(1)]
struct Empty;
fn main() {}
