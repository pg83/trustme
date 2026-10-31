//@ compile-fail: invalid left-hand side of assignment
//@ edition: 2024
#![feature(gen_blocks)]
fn main() { (gen { yield 1u8; }) = gen { yield 2u8; }; }
