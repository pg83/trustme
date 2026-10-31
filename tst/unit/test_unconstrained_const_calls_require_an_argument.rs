//@ compile-fail: Failed to infer const argument
#![feature(generic_arg_infer)]
fn number<const N: usize>() -> usize { N }
fn main() { let _: usize = number::<_>(); }
