//@ compile-fail: only be applied to functions
#![deny(unused_attributes)]
struct Record { #[proc_macro_derive(Mark)] value: u8 }
fn main() {}
