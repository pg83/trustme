//@ compile-fail: Unexpected token
extern crate "std" as renamed;
fn main() { assert_eq!(renamed::mem::size_of::<u16>(), 2); }
