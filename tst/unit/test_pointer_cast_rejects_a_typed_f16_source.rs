//@ compile-fail: Invalid cast to pointer
#![feature(f16)]
fn main() { let _ = 1.5f16 as *const u8; }
