//@ compile-fail: cannot be used in a deref pattern
#![feature(deref_patterns)]
fn main() { let deref!(_) = 13u32; }
