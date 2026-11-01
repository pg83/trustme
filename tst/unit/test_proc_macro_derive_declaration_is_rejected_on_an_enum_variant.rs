//@ compile-fail: only be applied to functions
#![deny(unused_attributes)]
enum Choice { #[proc_macro_derive(Mark)] Value }
fn main() {}
