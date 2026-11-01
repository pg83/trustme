//@ compile-fail: only be applied to functions
#![deny(unused_attributes)]
union Bits { #[proc_macro_derive(Mark)] byte: u8, word: u16 }
fn main() {}
