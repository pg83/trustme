//@ compile-fail: #[rustc_intrinsic] on non-function
#![feature(rustc_attrs)]
#[rustc_intrinsic]
struct Token;
fn main() {}
