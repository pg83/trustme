//@ aux-build: own_runtime_first.rs
//@ compile-fail: Type mismatch
extern crate own_runtime_first;
fn main() { let _: u8 = own_runtime_first::flag(); }
