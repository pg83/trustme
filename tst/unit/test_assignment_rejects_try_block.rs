//@ compile-fail: invalid left-hand side of assignment
#![feature(try_blocks)]
fn main() { (try { 1u8 }) = Some(2u8); }
