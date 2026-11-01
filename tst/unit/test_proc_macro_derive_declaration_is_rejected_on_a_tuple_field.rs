//@ compile-fail: only be applied to functions
#![deny(unused_attributes)]
struct Pair(#[proc_macro_derive(Mark)] u8, u16);
fn main() {}
