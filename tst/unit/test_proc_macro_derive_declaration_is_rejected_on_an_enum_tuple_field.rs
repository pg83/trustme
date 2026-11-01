//@ compile-fail: only be applied to functions
#![deny(unused_attributes)]
enum Choice { Value(#[proc_macro_derive(Mark)] u8) }
fn main() {}
