//@ compile-fail: Invalid cast to pointer
#![feature(f128)]
fn main() { let _ = 1.5f128 as *const u8; }
